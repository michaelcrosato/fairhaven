#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2NPCDirectorTests
{
	/** Real subsystem and actors, with no map, renderer or advancing sky actor. */
	struct FTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;

		explicit FTestWorld(bool bCapture = false)
		{
			if (!World) { return; }
			Director = UUEGT2NPCDirector::Get(World);
			Sky = World->SpawnActor<AUEGT2SkyController>();
			if (!Director || !Sky) { return; }
			Sky->TimeOfDay = 10.0f;
			Sky->SetDayLengthMinutes(4.0f); // 0.1 world hours per second
			// Only the director reads this temporary command line. No world BeginPlay
			// runs, so a capture test cannot start the real screenshot subsystem.
			const FString CommandLine = FCommandLine::Get();
			FCommandLine::Set(bCapture ? TEXT("-UEGT2Capture=DirectorTest") : TEXT(""));
			Director->OnWorldBeginPlay(*World);
			FCommandLine::Set(*CommandLine);
			Director->SetCrowdDensity(1.0f);
			Director->SetSchedulesPaused(true);
		}

		~FTestWorld() { if (World) { World->DestroyWorld(false); } }

		bool IsValid() const { return World && Director && Sky; }

		AUEGT2NPCActor* AddWorker()
		{
			AUEGT2NPCActor* NPC = World->SpawnActor<AUEGT2NPCActor>();
			if (NPC)
			{
				NPC->ConfigureNPC(TEXT("Test smith"), EUEGT2NPCRole::Smith,
					EUEGT2NPCSpecies::Person, 4242);
				NPC->DispatchBeginPlay();
				FUEGT2NPCContext Context;
				Context.Hour = 10.0f;
				NPC->EvaluateSchedule(Context, true);
			}
			return NPC;
		}

		/** Visit every pending slice without adding any more simulation time. */
		void Flush()
		{
			Sky->SetDayNightCycleEnabled(false);
			for (int32 Pass = 0; Pass < 6; ++Pass) { Director->Tick(0.25f); }
		}
	};

	struct FLife
	{
		FUEGT2NPCNeeds Needs;
		FUEGT2Purse Purse;
		EUEGT2Activity Activity;
		EUEGT2NPCRole Role;

		explicit FLife(const AUEGT2NPCActor* NPC)
			: Needs(NPC->GetNeeds()), Purse(NPC->GetPurse()),
			  Activity(NPC->GetActivity()), Role(NPC->GetNPCRole()) {}
	};

	void CheckLife(FAutomationTestBase& Test, const AUEGT2NPCActor* NPC,
		FLife Expected, float Hours, const FString& Label)
	{
		UEGT2AdvanceLife(Hours, Expected.Activity, Expected.Role, Expected.Needs, Expected.Purse);
		const FUEGT2NPCNeeds& Actual = NPC->GetNeeds();
		const float ActualValues[] = { Actual.Energy, Actual.Fed, Actual.Relief,
			Actual.Company, NPC->GetPurse().Coins };
		const float ExpectedValues[] = { Expected.Needs.Energy, Expected.Needs.Fed,
			Expected.Needs.Relief, Expected.Needs.Company, Expected.Purse.Coins };
		const TCHAR* Names[] = { TEXT("energy"), TEXT("fed"), TEXT("relief"), TEXT("company"), TEXT("coins") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			Test.TestTrue(FString::Printf(TEXT("%s %s: %.5f equals %.5f after %.3f h"),
				*Label, Names[Index], ActualValues[Index], ExpectedValues[Index], Hours),
				FMath::IsNearlyEqual(ActualValues[Index], ExpectedValues[Index], 0.0001f));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DirectorElapsedTimeTest,
	"UEGT2.NPC.Director.ElapsedTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DirectorElapsedTimeTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCDirectorTests;
	for (int32 Count : { 1, 7, 19, 36 })
	{
		FTestWorld Sim;
		if (!TestTrue(TEXT("isolated director is initialized"), Sim.IsValid())) { return false; }
		TArray<AUEGT2NPCActor*> Population;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			AUEGT2NPCActor* NPC = Sim.AddWorker();
			if (!TestNotNull(TEXT("worker spawned"), NPC)) { return false; }
			Population.Add(NPC);
		}
		Sim.Director->Tick(0.0f);
		const FLife Before(Population[0]); // identical identities, activities and initial needs
		float Hours = 0.0f;
		for (float Seconds : { 0.016f, 0.234f, 0.25f, 0.25f, 1.25f, 0.1f, 0.4f, 0.75f })
		{
			Hours += Seconds * 0.1f;
			Sim.Director->Tick(Seconds);
		}
		Sim.Flush();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			CheckLife(*this, Population[Index], Before, Hours,
				FString::Printf(TEXT("population %d, worker %d"), Count, Index));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DirectorRegistryTimeTest,
	"UEGT2.NPC.Director.RegistryTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DirectorRegistryTimeTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCDirectorTests;
	FTestWorld Sim;
	if (!TestTrue(TEXT("isolated director is initialized"), Sim.IsValid())) { return false; }
	TArray<AUEGT2NPCActor*> Population;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		AUEGT2NPCActor* NPC = Sim.AddWorker();
		if (!TestNotNull(TEXT("worker spawned"), NPC)) { return false; }
		Population.Add(NPC);
	}
	Sim.Director->Tick(0.0f);
	const FLife Before(Population[0]);
	Sim.Director->Tick(0.25f);
	Sim.Director->RegisterNPC(Population[4]); // duplicate registration must not reset pending time
	TestEqual(TEXT("duplicate registration leaves the population alone"), Sim.Director->GetPopulation(), 7);
	Sim.Director->UnregisterNPC(Population[0]);
	CheckLife(*this, Population[0], Before, 0.025f, TEXT("removed worker settles its pending time"));
	Sim.Director->Tick(0.25f);
	CheckLife(*this, Population[4], Before, 0.05f, TEXT("removal preserves the next slice's worker"));
	CheckLife(*this, Population[0], Before, 0.025f, TEXT("unregistered worker stops advancing"));

	AUEGT2NPCActor* Newcomer = Sim.AddWorker();
	if (!TestNotNull(TEXT("late worker spawned"), Newcomer)) { return false; }
	const FLife NewBefore(Newcomer);
	const FLife RejoinBefore(Population[0]);
	Sim.Director->RegisterNPC(Population[0]);
	Sim.Director->Tick(0.5f);
	Sim.Flush();
	CheckLife(*this, Newcomer, NewBefore, 0.05f, TEXT("new worker is not charged before registration"));
	CheckLife(*this, Population[0], RejoinBefore, 0.05f, TEXT("returning worker is not charged for its absence"));
	for (int32 Index = 1; Index < Population.Num(); ++Index)
	{
		CheckLife(*this, Population[Index], Before, 0.1f, TEXT("continuing worker keeps its clock"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DirectorDensityTimeTest,
	"UEGT2.NPC.Director.DensityTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DirectorDensityTimeTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCDirectorTests;
	FTestWorld Sim;
	if (!TestTrue(TEXT("isolated director is initialized"), Sim.IsValid())) { return false; }
	AUEGT2NPCActor* NPC = Sim.AddWorker();
	if (!TestNotNull(TEXT("worker spawned"), NPC)) { return false; }
	Sim.Director->Tick(0.0f);
	const FLife Before(NPC);
	Sim.Director->Tick(0.1f); // no slice yet: changing density must settle this interval
	Sim.Director->SetCrowdDensity(0.1f);
	if (!TestTrue(TEXT("test identity is hidden at low density"), NPC->IsSuppressed())) { return false; }
	CheckLife(*this, NPC, Before, 0.01f, TEXT("hiding settles the visible interval"));
	Sim.Director->Tick(0.4f);
	Sim.Director->SetCrowdDensity(1.0f);
	Sim.Director->Tick(0.1f);
	Sim.Flush();
	CheckLife(*this, NPC, Before, 0.02f, TEXT("showing does not charge the hidden interval"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DirectorClockTest,
	"UEGT2.NPC.Director.Clock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DirectorClockTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NPCDirectorTests;
	{
		FTestWorld Sim;
		if (!TestTrue(TEXT("isolated director is initialized"), Sim.IsValid())) { return false; }
		AUEGT2NPCActor* NPC = Sim.AddWorker();
		if (!TestNotNull(TEXT("worker spawned"), NPC)) { return false; }
		Sim.Director->Tick(0.0f);
		const FLife Before(NPC);
		Sim.Director->Tick(0.25f); // .025 h
		Sim.Sky->SetDayLengthMinutes(2.0f);
		Sim.Director->Tick(0.25f); // .050 h
		Sim.Sky->SetDayNightCycleEnabled(false);
		TestEqual(TEXT("disabled cycle exposes zero life rate"), Sim.Director->GetWorldHoursPerSecond(), 0.0f);
		Sim.Director->Tick(0.75f);
		CheckLife(*this, NPC, Before, 0.075f, TEXT("disabled cycle freezes needs and wages"));
		Sim.Sky->SetDayNightCycleEnabled(true);
		Sim.Director->Tick(0.25f); // .050 h
		Sim.Sky->SetDayLengthMinutes(0.0f);
		Sim.Director->Tick(0.5f);
		CheckLife(*this, NPC, Before, 0.125f, TEXT("zero day length freezes life"));
		Sim.Sky->SetDayLengthMinutes(4.0f);
		FTickableGameObject::TickObjects(Sim.World, LEVELTICK_All, true, 3.0f);
		CheckLife(*this, NPC, Before, 0.125f, TEXT("paused world does not advance its director"));
		Sim.Sky->SetTimeOfDay(22.0f);
		Sim.Director->Tick(0.25f); // .025 h, no invented hours from scrubbing the clock
		CheckLife(*this, NPC, Before, 0.15f, TEXT("each interval uses its own clock rate"));
		TestEqual(TEXT("paused schedules retain the current job"), NPC->GetActivity(), Before.Activity);
	}
	{
		FTestWorld Sim(true);
		if (!TestTrue(TEXT("capture director is initialized"), Sim.IsValid())) { return false; }
		AUEGT2NPCActor* NPC = Sim.AddWorker();
		if (!TestNotNull(TEXT("capture worker spawned"), NPC)) { return false; }
		Sim.Director->Tick(0.0f);
		const FLife Before(NPC);
		TestTrue(TEXT("capture freezes the director"), Sim.Director->IsFrozen());
		Sim.Director->Tick(2.0f);
		// Even with a nonzero sky rate, frozen time must not accumulate and
		// become payable later through a registry or density transition.
		Sim.Director->SetCrowdDensity(0.1f);
		Sim.Director->SetCrowdDensity(1.0f);
		CheckLife(*this, NPC, Before, 0.0f, TEXT("capture does not accumulate hidden debt"));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
