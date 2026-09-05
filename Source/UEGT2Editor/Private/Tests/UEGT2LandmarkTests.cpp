// Fairhaven (UEGT2) - restored discoveries belong to live actors in one world.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/UEGT2WorldInteractables.h"

namespace UEGT2LandmarkTests
{
	struct FWorld
	{
		UWorld* World = nullptr;

		FWorld()
		{
			if (!GEngine) { return; }
			World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
			if (World)
			{
				// Actor destruction uses the engine context even in a preview world.
				GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
			}
		}

		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		AUEGT2Landmark* Add(const FName Id)
		{
			AUEGT2Landmark* Landmark = World ? World->SpawnActor<AUEGT2Landmark>() : nullptr;
			if (Landmark)
			{
				Landmark->PersistentId = Id;
				Landmark->DispatchBeginPlay();
			}
			return Landmark;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2LandmarkRestoreTest,
	"UEGT2.Landmarks.Restoration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2LandmarkRestoreTest::RunTest(const FString& Parameters)
{
	UEGT2LandmarkTests::FWorld Sim;
	AUEGT2Landmark* Landmark = Sim.Add(TEXT("fairhaven_square"));
	if (!TestNotNull(TEXT("landmark exists"), Landmark)) { return false; }

	Landmark->SetLandmarkName(FText::FromString(TEXT("Renamed square")));
	TestEqual(TEXT("display text can change without changing save identity"),
		Landmark->GetPersistentId(), FName(TEXT("fairhaven_square")));
	TestEqual(TEXT("display name is available to progress UI"),
		Landmark->GetLandmarkName().ToString(), FString(TEXT("Renamed square")));
	TestFalse(TEXT("a new landmark is undiscovered"), Landmark->IsDiscovered());
	TestEqual(TEXT("an undiscovered landmark still contributes to the total"),
		AUEGT2Landmark::GetTotalCount(Sim.World), 1);

	Landmark->SetDiscovered(true);
	Landmark->SetDiscovered(true);
	TestTrue(TEXT("restoration applies discovery"), Landmark->IsDiscovered());
	TestEqual(TEXT("repeated restoration counts the discovery once"),
		AUEGT2Landmark::GetDiscoveredCount(Sim.World), 1);
	TestFalse(TEXT("a restored discovery is no longer usable"), Landmark->CanInteract(nullptr));
	Landmark->Interact(nullptr);
	TestEqual(TEXT("using a restored single-use landmark cannot count it again"),
		AUEGT2Landmark::GetDiscoveredCount(Sim.World), 1);

	Landmark->SetDiscovered(false);
	Landmark->SetDiscovered(false);
	TestEqual(TEXT("repeated reset clears the count"), AUEGT2Landmark::GetDiscoveredCount(Sim.World), 0);
	TestTrue(TEXT("reset makes the landmark usable again"), Landmark->CanInteract(nullptr));
	Landmark->SetDiscovered(true);
	TestEqual(TEXT("loading after reset restores one discovery"),
		AUEGT2Landmark::GetDiscoveredCount(Sim.World), 1);
	TestEqual(TEXT("a null world has no discoveries"), AUEGT2Landmark::GetDiscoveredCount(nullptr), 0);
	TestEqual(TEXT("a null world has no landmarks"), AUEGT2Landmark::GetTotalCount(nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2LandmarkWorldIsolationTest,
	"UEGT2.Landmarks.WorldIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2LandmarkWorldIsolationTest::RunTest(const FString& Parameters)
{
	UEGT2LandmarkTests::FWorld FirstWorld;
	UEGT2LandmarkTests::FWorld SecondWorld;
	AUEGT2Landmark* First = FirstWorld.Add(TEXT("fairhaven_square"));
	AUEGT2Landmark* Remaining = FirstWorld.Add(TEXT("fairhaven_harbour"));
	AUEGT2Landmark* OtherWorld = SecondWorld.Add(TEXT("fairhaven_square"));
	if (!TestNotNull(TEXT("first landmark"), First)
		|| !TestNotNull(TEXT("remaining landmark"), Remaining)
		|| !TestNotNull(TEXT("other world's landmark"), OtherWorld)) { return false; }

	First->SetDiscovered(true);
	Remaining->SetDiscovered(true);
	TestEqual(TEXT("first world counts only its landmarks"), AUEGT2Landmark::GetTotalCount(FirstWorld.World), 2);
	TestEqual(TEXT("second world counts only its landmark"), AUEGT2Landmark::GetTotalCount(SecondWorld.World), 1);
	TestEqual(TEXT("first world has two discoveries"), AUEGT2Landmark::GetDiscoveredCount(FirstWorld.World), 2);
	TestEqual(TEXT("matching identity in another world does not inherit discovery"),
		AUEGT2Landmark::GetDiscoveredCount(SecondWorld.World), 0);
	OtherWorld->SetDiscovered(true);

	TestTrue(TEXT("one discovered actor can end play"), First->Destroy());
	TestEqual(TEXT("destroyed actor leaves the first world's total"), AUEGT2Landmark::GetTotalCount(FirstWorld.World), 1);
	TestEqual(TEXT("ending one actor preserves the remaining discovery"),
		AUEGT2Landmark::GetDiscoveredCount(FirstWorld.World), 1);
	TestEqual(TEXT("ending one actor preserves the other world's total"), AUEGT2Landmark::GetTotalCount(SecondWorld.World), 1);
	TestEqual(TEXT("ending one actor preserves the other world's discovery"),
		AUEGT2Landmark::GetDiscoveredCount(SecondWorld.World), 1);
	Remaining->SetDiscovered(false);
	TestEqual(TEXT("reset in one world does not reset the other"),
		AUEGT2Landmark::GetDiscoveredCount(SecondWorld.World), 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
