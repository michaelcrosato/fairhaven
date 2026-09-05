#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2NeedsComponent.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2NeedsComponentTests
{
	struct FLife
	{
		FUEGT2NPCNeeds Needs;
		FUEGT2Purse Purse;
		EUEGT2Activity Activity;
		EUEGT2NPCRole Role;

		explicit FLife(const UUEGT2NeedsComponent* Player)
			: Needs(Player->GetNeeds()), Purse(Player->GetPurse()),
			  Activity(Player->GetActivity()), Role(Player->GetTrade()) {}

		explicit FLife(const AUEGT2NPCActor* NPC)
			: Needs(NPC->GetNeeds()), Purse(NPC->GetPurse()),
			  Activity(NPC->GetActivity()), Role(NPC->GetNPCRole()) {}

		void Advance(float Hours) { UEGT2AdvanceLife(Hours, Activity, Role, Needs, Purse); }
	};

	/** Registered component, real director and one worker; no map or world tick. */
	struct FTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		UUEGT2NeedsComponent* Player = nullptr;
		AUEGT2NPCActor* Worker = nullptr;
		bool bWorking = false;

		explicit FTestWorld(bool bCapture = false)
		{
			if (!World) { return; }
			Director = UUEGT2NPCDirector::Get(World);
			Sky = World->SpawnActor<AUEGT2SkyController>();
			AActor* Owner = World->SpawnActor<AActor>();
			AActor* Venue = World->SpawnActor<AActor>();
			Worker = World->SpawnActor<AUEGT2NPCActor>();
			if (!Director || !Sky || !Owner || !Venue || !Worker) { return; }

			Sky->TimeOfDay = 10.0f;
			Sky->SetDayLengthMinutes(4.0f);
			// Initialize only the director: a capture fixture must not start the
			// actual capture subsystem or run Sky::BeginPlay's clock override.
			const FString CommandLine = FCommandLine::Get();
			FCommandLine::Set(bCapture ? TEXT("-UEGT2Capture=NeedsTest") : TEXT(""));
			Director->OnWorldBeginPlay(*World);
			FCommandLine::Set(*CommandLine);
			Director->SetCrowdDensity(1.0f);
			Director->SetSchedulesPaused(true);

			Worker->ConfigureNPC(TEXT("Test smith"), EUEGT2NPCRole::Smith,
				EUEGT2NPCSpecies::Person, 4242);
			Worker->DispatchBeginPlay();
			Director->Tick(0.0f);

			Player = NewObject<UUEGT2NeedsComponent>(Owner, TEXT("Life"));
			Owner->AddInstanceComponent(Player);
			Player->RegisterComponent();
			Owner->DispatchBeginPlay();
			bWorking = Player->BeginActivity(EUEGT2Activity::Work, Venue,
				FText::FromString(TEXT("Test smithy")), EUEGT2NPCRole::Smith, 400.0f);
		}

		~FTestWorld() { if (World) { World->DestroyWorld(false); } }

		bool IsValid() const
		{
			return World && Director && Sky && Player && Worker && bWorking
				&& Player->IsRegistered() && Player->HasBegunPlay()
				&& Worker->GetActivity() == EUEGT2Activity::Work;
		}

		void Tick(float Seconds)
		{
			Director->Tick(Seconds);
			Player->TickComponent(Seconds, LEVELTICK_All, &Player->PrimaryComponentTick);
			// Short component intervals can leave the worker waiting for its
			// schedule slice. Settle it without adding any more world hours.
			const bool bCycleEnabled = Sky->IsDayNightCycleEnabled();
			Sky->SetDayNightCycleEnabled(false);
			Director->Tick(0.25f);
			Sky->SetDayNightCycleEnabled(bCycleEnabled);
		}
	};

	void CheckLife(FAutomationTestBase& Test, const FLife& Actual, const FLife& Expected,
		const FString& Label)
	{
		const float ActualValues[] = { Actual.Needs.Energy, Actual.Needs.Fed,
			Actual.Needs.Relief, Actual.Needs.Company, Actual.Purse.Coins };
		const float ExpectedValues[] = { Expected.Needs.Energy, Expected.Needs.Fed,
			Expected.Needs.Relief, Expected.Needs.Company, Expected.Purse.Coins };
		const TCHAR* Names[] = { TEXT("energy"), TEXT("fed"), TEXT("relief"), TEXT("company"), TEXT("coins") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			Test.TestTrue(FString::Printf(TEXT("%s %s: %.5f equals %.5f"),
				*Label, Names[Index], ActualValues[Index], ExpectedValues[Index]),
				FMath::IsNearlyEqual(ActualValues[Index], ExpectedValues[Index], 0.0001f));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PlayerElapsedTimeTest,
	"UEGT2.Player.Needs.ElapsedTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PlayerElapsedTimeTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NeedsComponentTests;
	struct FCase { float DayMinutes; float Seconds; };
	for (const FCase& Case : { FCase{20.0f, 20.0f}, FCase{1.0f, 2.0f}, FCase{0.1f, 0.1f} })
	{
		FTestWorld Sim;
		if (!TestTrue(TEXT("real needs component and worker begin working"), Sim.IsValid())) { return false; }
		Sim.Sky->SetDayLengthMinutes(Case.DayMinutes);
		FLife ExpectedPlayer(Sim.Player), ExpectedWorker(Sim.Worker);
		const float Hours = Case.Seconds * 24.0f / (Case.DayMinutes * 60.0f);
		ExpectedPlayer.Advance(Hours);
		ExpectedWorker.Advance(Hours);
		Sim.Tick(Case.Seconds);
		const FString Label = FString::Printf(TEXT("%.1fs at %.1f min/day"), Case.Seconds, Case.DayMinutes);
		CheckLife(*this, FLife(Sim.Player), ExpectedPlayer, Label + TEXT(" player"));
		CheckLife(*this, FLife(Sim.Worker), ExpectedWorker, Label + TEXT(" worker"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PlayerLifeClockTest,
	"UEGT2.Player.Needs.Clock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PlayerLifeClockTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NeedsComponentTests;
	{
		FTestWorld Sim;
		if (!TestTrue(TEXT("real needs component and worker begin working"), Sim.IsValid())) { return false; }
		FLife ExpectedPlayer(Sim.Player), ExpectedWorker(Sim.Worker);
		const auto Step = [&](float Seconds, float Hours, const TCHAR* Label)
		{
			ExpectedPlayer.Advance(Hours);
			ExpectedWorker.Advance(Hours);
			Sim.Tick(Seconds);
			CheckLife(*this, FLife(Sim.Player), ExpectedPlayer, FString(Label) + TEXT(" player"));
			CheckLife(*this, FLife(Sim.Worker), ExpectedWorker, FString(Label) + TEXT(" worker"));
		};
		Step(0.25f, 0.025f, TEXT("initial rate"));
		Sim.Sky->SetDayLengthMinutes(2.0f);
		Step(0.25f, 0.05f, TEXT("changed rate"));
		Sim.Sky->SetDayNightCycleEnabled(false);
		Step(3.0f, 0.0f, TEXT("disabled cycle"));
		Sim.Sky->SetDayNightCycleEnabled(true);
		Step(0.25f, 0.05f, TEXT("resumed cycle"));
		Sim.Sky->SetDayLengthMinutes(0.0f);
		Step(3.0f, 0.0f, TEXT("zero day length"));
		Sim.Sky->SetDayLengthMinutes(4.0f);
		Sim.Sky->SetTimeOfDay(22.0f);
		Step(0.0f, 0.0f, TEXT("clock scrub creates no life delta"));
		Step(0.25f, 0.025f, TEXT("time resumes after clock scrub"));
	}
	{
		FTestWorld Sim(true);
		if (!TestTrue(TEXT("capture needs component and worker begin working"), Sim.IsValid())) { return false; }
		TestTrue(TEXT("capture freezes the director"), Sim.Director->IsFrozen());
		Sim.Sky->SetDayLengthMinutes(1.0f);
		const FLife BeforePlayer(Sim.Player), BeforeWorker(Sim.Worker);
		Sim.Tick(2.0f);
		CheckLife(*this, FLife(Sim.Player), BeforePlayer, TEXT("capture freezes player life"));
		CheckLife(*this, FLife(Sim.Worker), BeforeWorker, TEXT("capture freezes worker life"));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
