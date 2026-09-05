#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "NPC/UEGT2NPCRoutines.h"
#include "NPC/UEGT2RouteNetwork.h"
#include "World/UEGT2Almanac.h"
#include "World/UEGT2SkyController.h"
#include <limits>

namespace UEGT2RestSimulationTests
{
	FUEGT2NPCContext Healthy(float Hour = 10.0f)
	{
		FUEGT2NPCContext Context;
		Context.Hour = Hour;
		Context.Personality.Curiosity = 0.0f;
		Context.Personality.Sociability = 1.0f;
		Context.Needs.Energy = Context.Needs.Fed = Context.Needs.Relief = Context.Needs.Company = 0.8f;
		Context.Purse.Coins = 10.625f;
		// Every pure simulation must suppress this otherwise overriding greeting.
		Context.PlayerDistance = 0.0f;
		return Context;
	}

	FUEGT2NPCContext Snapshot(const AUEGT2NPCActor* NPC, int32 Day, float Hour)
	{
		FUEGT2NPCContext Context;
		Context.DayIndex = Day;
		Context.Hour = Hour;
		Context.Needs = NPC->GetNeeds();
		Context.Purse = NPC->GetPurse();
		Context.Personality = NPC->GetPersonality();
		Context.Seed = NPC->GetSeed();
		Context.bExposed = !NPC->IsIndoors();
		return Context;
	}

	void CheckLife(FAutomationTestBase& Test, const FUEGT2NPCContext& Actual,
		const FUEGT2NPCContext& Expected, const FString& Label)
	{
		const float A[] = { Actual.Needs.Energy, Actual.Needs.Fed, Actual.Needs.Relief,
			Actual.Needs.Company, Actual.Purse.Coins };
		const float E[] = { Expected.Needs.Energy, Expected.Needs.Fed, Expected.Needs.Relief,
			Expected.Needs.Company, Expected.Purse.Coins };
		const TCHAR* Names[] = { TEXT("energy"), TEXT("fed"), TEXT("relief"), TEXT("company"), TEXT("coins") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(A); ++Index)
		{
			Test.TestTrue(Label + TEXT(" ") + Names[Index], FMath::IsNearlyEqual(A[Index], E[Index], 0.0002f));
		}
	}

	struct FTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		AUEGT2RouteNetwork* Routes = nullptr;

		explicit FTestWorld(bool bCapture = false, bool bCreateSky = true)
		{
			if (!World) { return; }
			Director = UUEGT2NPCDirector::Get(World);
			if (bCreateSky)
			{
				Sky = World->SpawnActor<AUEGT2SkyController>();
				Sky->TimeOfDay = 10.0f;
				Sky->SetDayLengthMinutes(4.0f);
			}
			Routes = World->SpawnActor<AUEGT2RouteNetwork>();
			Routes->LinkNodes(Routes->AddNode(FVector::ZeroVector), Routes->AddNode(FVector(4000, 0, 0)));
			Routes->FinaliseNetwork();
			const FString CommandLine = FCommandLine::Get();
			FCommandLine::Set(bCapture ? TEXT("-UEGT2Capture=RestTest") : TEXT(""));
			Director->OnWorldBeginPlay(*World);
			FCommandLine::Set(*CommandLine);
			Director->SetCrowdDensity(1.0f);
			Director->SetSchedulesPaused(false);
		}

		~FTestWorld() { if (World) { World->DestroyWorld(false); } }
		bool IsValid() const { return World && Director && Routes; }

		AUEGT2NPCActor* AddWorker(EUEGT2NPCSpecies Species = EUEGT2NPCSpecies::Person)
		{
			AUEGT2NPCActor* NPC = World->SpawnActor<AUEGT2NPCActor>();
			if (NPC)
			{
				NPC->ConfigureNPC(TEXT("Rest test inhabitant"), EUEGT2NPCRole::Smith, Species, 4242);
				NPC->AddAnchor(EUEGT2Anchor::Home, FVector::ZeroVector);
				NPC->AddAnchor(EUEGT2Anchor::Work, FVector(4000, 0, 0));
				NPC->DispatchBeginPlay();
			}
			return NPC;
		}

		void FlushWithoutTime()
		{
			Sky->SetDayNightCycleEnabled(false);
			for (int32 Pass = 0; Pass < 6; ++Pass) { Director->Tick(0.25f); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestCalendarTest, "UEGT2.Rest.Simulation.Calendar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestCalendarTest::RunTest(const FString& Parameters)
{
	struct FCase { int32 Day; float Hour; int32 Wake; int32 ExpectedDay; float Hours; };
	const FCase Cases[] = {
		{ 0, 10.25f, 12, 0, 1.75f }, { 7, 8.0f, 8, 8, 24.0f },
		{ 29, 23.75f, 0, 30, 0.25f }, { 359, 22.5f, 7, 360, 8.5f },
		{ 1000000, 7.0f, 8, 1000000, 1.0f }, { 0, 7.99999f, 8, 0, 8.0f - 7.99999f }
	};
	FUEGT2RestPreview Preview;
	for (const FCase& Case : Cases)
	{
		TestTrue(TEXT("valid next occurrence"), UUEGT2NPCDirector::CalculateRestPreview(Case.Day, Case.Hour, Case.Wake, Preview));
		TestEqual(TEXT("start day"), Preview.StartDayIndex, Case.Day);
		TestEqual(TEXT("start hour"), Preview.StartHour, Case.Hour);
		TestEqual(TEXT("wake day"), Preview.WakeDayIndex, Case.ExpectedDay);
		TestEqual(TEXT("wake hour"), Preview.WakeHour, Case.Wake);
		TestEqual(TEXT("exact duration"), Preview.DurationHours, Case.Hours);
	}
	UUEGT2NPCDirector::CalculateRestPreview(29, 23.0f, 0, Preview);
	const FUEGT2Date Month = UEGT2DateFromDayIndex(Preview.WakeDayIndex);
	TestEqual(TEXT("month begins on its first day"), Month.Day, 1);
	TestEqual(TEXT("month rolls forward"), Month.Month, 2);
	UUEGT2NPCDirector::CalculateRestPreview(359, 23.0f, 0, Preview);
	TestEqual(TEXT("year rolls forward"), UEGT2DateFromDayIndex(Preview.WakeDayIndex).Year, 2);
	for (const FCase& Invalid : { FCase{ -1, 1, 2, 0, 0 }, FCase{ 1000000, 8, 8, 0, 0 },
		FCase{ 0, 24, 0, 0, 0 }, FCase{ 0, 1, 24, 0, 0 }, FCase{ 0, 1, -1, 0, 0 },
		FCase{ 0, std::numeric_limits<float>::quiet_NaN(), 8, 0, 0 } })
	{
		const FUEGT2RestPreview Before = Preview;
		TestFalse(TEXT("invalid calendar rejected"), UUEGT2NPCDirector::CalculateRestPreview(Invalid.Day, Invalid.Hour, Invalid.Wake, Preview));
		TestEqual(TEXT("failed preview leaves output intact"), Preview.WakeDayIndex, Before.WakeDayIndex);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestScheduledLedgerTest, "UEGT2.Rest.Simulation.ScheduledLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestScheduledLedgerTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestSimulationTests;
	const EUEGT2NPCRole Role = EUEGT2NPCRole::Smith;
	const EUEGT2NPCSpecies Person = EUEGT2NPCSpecies::Person;
	// This interval straddles work, a free scheduled lunch, then work again.
	FUEGT2NPCContext Lunch = Healthy(12.0f);
	FUEGT2NPCContext Expected = Lunch;
	UEGT2AdvanceLife(0.25f, EUEGT2Activity::Work, Role, Expected.Needs, Expected.Purse);
	UEGT2AdvanceLife(1.0f, EUEGT2Activity::Lunch, Role, Expected.Needs, Expected.Purse);
	UEGT2AdvanceLife(0.25f, EUEGT2Activity::Work, Role, Expected.Needs, Expected.Purse);
	TestTrue(TEXT("whole work/lunch interval succeeds"), UEGT2AdvanceScheduledLife(Role, Person, 1.5f, Lunch));
	CheckLife(*this, Lunch, Expected, TEXT("schedule transitions and shared meal/wage rates"));
	TestEqual(TEXT("scheduled clock advances"), Lunch.Hour, 13.5f);

	FUEGT2NPCContext Broke = Healthy();
	Broke.Needs.Fed = 0.1f;
	Broke.Purse.Coins = 0.0f;
	Expected = Broke;
	UEGT2AdvanceLife(1.0f / 60.0f, EUEGT2Activity::Work, Role, Expected.Needs, Expected.Purse);
	TestTrue(TEXT("broke worker can earn"), UEGT2AdvanceScheduledLife(Role, Person, 1.0f / 60.0f, Broke));
	CheckLife(*this, Broke, Expected, TEXT("unaffordable needs select work"));
	TestTrue(TEXT("subsequent hours escape empty purse hunger"), UEGT2AdvanceScheduledLife(Role, Person, 2.0f, Broke));
	TestTrue(TEXT("earned money pays for food"), Broke.Needs.Fed > 0.2f);

	FUEGT2NPCContext RestDay = Healthy();
	RestDay.DayIndex = 5;
	Expected = RestDay;
	UEGT2AdvanceLife(0.25f, EUEGT2Activity::Worship, Role, Expected.Needs, Expected.Purse);
	TestTrue(TEXT("rest-day schedule succeeds"), UEGT2AdvanceScheduledLife(Role, Person, 0.25f, RestDay));
	CheckLife(*this, RestDay, Expected, TEXT("rest day worship replaces the shift"));

	FUEGT2NPCContext Rain = Healthy();
	Rain.Weather = EUEGT2Weather::Storm;
	Expected = Rain;
	UEGT2AdvanceLife(0.25f, EUEGT2Activity::Shelter, Role, Expected.Needs, Expected.Purse);
	TestTrue(TEXT("wet-weather simulation succeeds"), UEGT2AdvanceScheduledLife(Role, Person, 0.25f, Rain));
	CheckLife(*this, Rain, Expected, TEXT("weather shelter replaces outdoor activity"));

	FUEGT2NPCContext Early = Healthy(7.8f), Late = Early;
	Early.Personality.Punctuality = 1.0f;
	Late.Personality.Punctuality = 0.0f;
	UEGT2AdvanceScheduledLife(Role, Person, 0.1f, Early);
	UEGT2AdvanceScheduledLife(Role, Person, 0.1f, Late);
	TestTrue(TEXT("personality changes when the paid shift begins"), Early.Purse.Coins > Late.Purse.Coins + 1.0f);

	FUEGT2NPCContext Bird = Healthy(23.75f);
	Bird.DayIndex = 359;
	Bird.Needs.Energy = 0.3f;
	Expected = Bird;
	Expected.Needs.Advance(0.5f, EUEGT2Activity::Roost);
	TestTrue(TEXT("bird rests through year boundary"), UEGT2AdvanceScheduledLife(Role, EUEGT2NPCSpecies::Chicken, 0.5f, Bird));
	CheckLife(*this, Bird, Expected, TEXT("animal rates ignore purse and nearby player"));
	TestEqual(TEXT("animal reaches next day"), Bird.DayIndex, 360);
	TestEqual(TEXT("animal retains fractional wake hour"), Bird.Hour, 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestInvalidModelTest, "UEGT2.Rest.Simulation.InvalidModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestInvalidModelTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestSimulationTests;
	for (float Hours : { -1.0f, 24.1f, std::numeric_limits<float>::infinity() })
	{
		FUEGT2NPCContext Context = Healthy();
		const FUEGT2NPCContext Before = Context;
		TestFalse(TEXT("invalid duration refused"), UEGT2AdvanceScheduledLife(EUEGT2NPCRole::Smith, EUEGT2NPCSpecies::Person, Hours, Context));
		CheckLife(*this, Context, Before, TEXT("invalid duration is atomic"));
		TestEqual(TEXT("invalid duration preserves hour"), Context.Hour, Before.Hour);
	}
	for (int32 Case = 0; Case < 7; ++Case)
	{
		FUEGT2NPCContext Context = Healthy();
		EUEGT2NPCRole Role = EUEGT2NPCRole::Smith;
		EUEGT2NPCSpecies Species = EUEGT2NPCSpecies::Person;
		if (Case == 0) { Context.Needs.Fed = -0.1f; }
		if (Case == 1) { Context.Personality.Bravery = 1.1f; }
		if (Case == 2) { Context.Purse.Coins = std::numeric_limits<float>::quiet_NaN(); }
		if (Case == 3) { Context.Weather = EUEGT2Weather::Count; }
		if (Case == 4) { Role = EUEGT2NPCRole::Count; }
		if (Case == 5) { Species = EUEGT2NPCSpecies::Count; }
		if (Case == 6) { Context.DayIndex = 1000000; }
		TestFalse(TEXT("invalid life or overflowing calendar refused"), UEGT2AdvanceScheduledLife(Role, Species, 24.0f, Context));
		TestEqual(TEXT("invalid snapshot keeps original energy"), Context.Needs.Energy, 0.8f);
		TestEqual(TEXT("invalid snapshot keeps original hour"), Context.Hour, 10.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestDirectorCommitTest, "UEGT2.Rest.Simulation.DirectorCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestDirectorCommitTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestSimulationTests;
	FTestWorld Sim;
	if (!TestTrue(TEXT("real rest world ready"), Sim.IsValid())) { return false; }
	TArray<AUEGT2NPCActor*> People;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		AUEGT2NPCActor* NPC = Sim.AddWorker(Index == 5 ? EUEGT2NPCSpecies::Chicken : EUEGT2NPCSpecies::Person);
		if (!TestNotNull(TEXT("inhabitant spawned"), NPC)) { return false; }
		People.Add(NPC);
	}
	Sim.Director->Tick(0.0f);
	People[0]->SetFollowTarget(People[1]);
	People[0]->Say(FText::FromString(TEXT("Before rest")), 5.0f, 0.0f);
	People.Last()->SetSuppressed(true);
	TArray<FUEGT2NPCContext> Expected;
	for (AUEGT2NPCActor* NPC : People)
	{
		FUEGT2NPCContext Context = Snapshot(NPC, 0, 10.0f);
		if (!NPC->IsSuppressed())
		{
			if (NPC->IsAnimal()) { Context.Needs.Advance(0.01f, NPC->GetActivity()); }
			else { UEGT2AdvanceLife(0.01f, NPC->GetActivity(), NPC->GetNPCRole(), Context.Needs, Context.Purse); }
			TestTrue(TEXT("expected scheduled life succeeds"), UEGT2AdvanceScheduledLife(NPC->GetNPCRole(), NPC->GetSpecies(), 2.0f, Context));
		}
		Expected.Add(Context);
		NPC->SetLOD(EUEGT2NPCLOD::Near);
		// Leave active workers a long way from their anchors, so the old snap
		// path would perform real A* searches before discarding those routes.
		if (!NPC->IsSuppressed()) { NPC->SetActorLocation(FVector::ZeroVector); }
	}
	const FVector SuppressedLocation = People.Last()->GetActorLocation();
	Sim.Director->Tick(0.1f); // less than the next slice: every actor has unpaid live time
	const int32 Searches = Sim.Routes->GetSearchCount();
	FUEGT2RestPreview Preview;
	FText Reason;
	TestTrue(TEXT("town rest commits"), Sim.Director->AdvanceForRest(12, Preview, Reason));
	TestEqual(TEXT("commit synchronizes sky"), Sim.Sky->GetTimeOfDay(), 12.0f);
	TestEqual(TEXT("commit synchronizes director"), Sim.Director->GetHour(), 12.0f);
	TestEqual(TEXT("rest performs no discarded route searches"), Sim.Routes->GetSearchCount(), Searches);
	TestFalse(TEXT("rest ends companionship"), People[0]->IsFollowing());
	TestFalse(TEXT("rest removes stale speech"), People[0]->HasBubble());
	TestTrue(TEXT("suppressed placement stays intact"), People.Last()->GetActorLocation().Equals(SuppressedLocation));
	for (int32 Index = 0; Index < People.Num(); ++Index)
	{
		CheckLife(*this, Snapshot(People[Index], 0, 12), Expected[Index], TEXT("committed ledger includes live debt exactly once"));
		if (!People[Index]->IsSuppressed())
		{
			FUEGT2NPCContext Final = Expected[Index];
			TestEqual(TEXT("final activity ignores the resting player"), People[Index]->GetActivity(),
				ResolveActivity(People[Index]->GetNPCRole(), People[Index]->GetSpecies(), Final).Activity);
		}
	}
	Sim.FlushWithoutTime();
	People.Last()->SetSuppressed(false);
	Sim.FlushWithoutTime();
	for (int32 Index = 0; Index < People.Num(); ++Index)
	{
		CheckLife(*this, Snapshot(People[Index], 0, 12), Expected[Index], TEXT("resumed slices and revealed NPC owe no skipped time"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestDirectorValidationTest, "UEGT2.Rest.Simulation.DirectorValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestDirectorValidationTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestSimulationTests;
	FTestWorld Sim;
	if (!TestTrue(TEXT("real rest world ready"), Sim.IsValid())) { return false; }
	AUEGT2NPCActor* First = Sim.AddWorker();
	AUEGT2NPCActor* Last = Sim.AddWorker();
	if (!TestNotNull(TEXT("first inhabitant"), First) || !TestNotNull(TEXT("last inhabitant"), Last)) { return false; }
	FUEGT2RestPreview Preview;
	FText Reason;
	TestFalse(TEXT("rest before first population snap rejected"), Sim.Director->AdvanceForRest(12, Preview, Reason));
	Sim.Director->Tick(0.0f);
	Sim.Director->Tick(0.1f);
	const FUEGT2NPCContext Before = Snapshot(First, 0, 10);
	const EUEGT2Activity OriginalActivity = First->GetActivity();
	const FVector Position = First->GetActorLocation();
	Last->ConfigureNPC(TEXT("Invalid authored role"), EUEGT2NPCRole::Count, EUEGT2NPCSpecies::Person, 4242);
	TestTrue(TEXT("cheap UI preview does not scan actor state"), Sim.Director->CanAdvanceForRest(12, Preview, Reason));
	TestFalse(TEXT("invalid last inhabitant rejects whole population"), Sim.Director->AdvanceForRest(12, Preview, Reason));
	CheckLife(*this, Snapshot(First, 0, 10), Before, TEXT("earlier candidate and pending debt remain untouched"));
	TestTrue(TEXT("failed rest preserves placement"), First->GetActorLocation().Equals(Position));
	TestEqual(TEXT("failed rest preserves sky"), Sim.Sky->GetTimeOfDay(), 10.0f);
	Last->ConfigureNPC(TEXT("Valid again"), EUEGT2NPCRole::Smith, EUEGT2NPCSpecies::Person, 4242);
	Sim.Director->SetSchedulesPaused(true);
	TestFalse(TEXT("paused routines rejected"), Sim.Director->AdvanceForRest(12, Preview, Reason));
	Sim.Director->SetSchedulesPaused(false);
	Sim.Sky->SetDayNightCycleEnabled(false);
	TestFalse(TEXT("stopped clock rejected"), Sim.Director->AdvanceForRest(12, Preview, Reason));
	Sim.Sky->SetDayNightCycleEnabled(true);
	Sim.Sky->SetDayLengthMinutes(0.0f);
	TestFalse(TEXT("zero clock rate rejected"), Sim.Director->AdvanceForRest(12, Preview, Reason));
	Sim.Sky->SetDayLengthMinutes(4.0f);
	TestTrue(TEXT("repaired state can commit"), Sim.Director->AdvanceForRest(12, Preview, Reason));
	FUEGT2NPCContext Expected = Before;
	UEGT2AdvanceLife(0.01f, OriginalActivity, First->GetNPCRole(), Expected.Needs, Expected.Purse);
	// The failed attempts left both elapsed baselines and the original activity untouched.
	UEGT2AdvanceScheduledLife(First->GetNPCRole(), First->GetSpecies(), 2.0f, Expected);
	CheckLife(*this, Snapshot(First, 0, 12), Expected, TEXT("retry pays original live debt once"));
	{
		FTestWorld Frozen(true);
		Frozen.AddWorker();
		Frozen.Director->Tick(0.0f);
		TestFalse(TEXT("capture-frozen population rejected"), Frozen.Director->AdvanceForRest(12, Preview, Reason));
	}
	{
		FTestWorld NoClock(false, false);
		NoClock.AddWorker();
		NoClock.Director->Tick(0.0f);
		TestFalse(TEXT("missing sky rejected"), NoClock.Director->AdvanceForRest(12, Preview, Reason));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestDirectorMidnightTest, "UEGT2.Rest.Simulation.DirectorMidnight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestDirectorMidnightTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestSimulationTests;
	FTestWorld Sim;
	if (!TestTrue(TEXT("real rest world ready"), Sim.IsValid()) || !TestNotNull(TEXT("inhabitant"), Sim.AddWorker())) { return false; }
	Sim.Director->Tick(0.0f);
	Sim.Director->RestoreCalendar(359, 23.99f, EUEGT2Weather::Cloudy);
	Sim.Sky->SetTimeOfDay(0.01f); // sky crossed midnight just before the world paused
	FUEGT2RestPreview Preview, Again, Committed;
	FText Reason;
	TestTrue(TEXT("midnight preview succeeds"), Sim.Director->CanAdvanceForRest(8, Preview, Reason));
	TestTrue(TEXT("repeated paused preview succeeds"), Sim.Director->CanAdvanceForRest(8, Again, Reason));
	TestEqual(TEXT("preview recognizes uncached next day"), Preview.StartDayIndex, 360);
	TestEqual(TEXT("preview does not mutate cached day"), Sim.Director->GetDayIndex(), 359);
	TestEqual(TEXT("paused preview stays stable"), Again.DurationHours, Preview.DurationHours);
	TestTrue(TEXT("midnight commit succeeds"), Sim.Director->AdvanceForRest(8, Committed, Reason));
	TestEqual(TEXT("commit uses displayed duration"), Committed.DurationHours, Preview.DurationHours);
	TestEqual(TEXT("commit uses displayed day"), Sim.Director->GetDayIndex(), 360);
	TestEqual(TEXT("rest preserves weather"), Sim.Sky->GetWeather(), EUEGT2Weather::Cloudy);
	TestEqual(TEXT("rest preserves clock rate"), Sim.Sky->GetDayLengthMinutes(), 4.0f);
	Sim.FlushWithoutTime();
	TestEqual(TEXT("first resumed tick does not count midnight twice"), Sim.Director->GetDayIndex(), 360);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
