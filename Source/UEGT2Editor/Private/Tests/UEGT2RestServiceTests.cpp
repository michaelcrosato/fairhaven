#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/UEGT2Amenity.h"
#include "Misc/CommandLine.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Rest/UEGT2RestSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "World/UEGT2SkyController.h"

#include <limits>

namespace UEGT2RestServiceTests
{
	struct FWorldScope
	{
		UWorld* World = nullptr;
		explicit FWorldScope(EWorldType::Type Type = EWorldType::Game)
		{
			if (!GEngine) { return; }
			World = UWorld::CreateWorld(Type, false);
			if (World) { GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World); }
		}
		~FWorldScope()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};

	/** Real services and actors, without controller BeginPlay or a viewport. */
	struct FFixture
	{
		FString OriginalCommandLine = FCommandLine::Get();
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bOriginalSetting = Settings && Settings->GetSleepUntilEnabled();
		FWorldScope Scope;
		UWorld* World = Scope.World;
		UUEGT2RestSubsystem* Rest = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		AUEGT2PlayerController* Controller = nullptr;
		AUEGT2Character* Player = nullptr;
		AUEGT2NPCActor* Worker = nullptr;
		AUEGT2Amenity* Bed = nullptr;
		APlayerState* Pauser = nullptr;
		bool bReady = false;

		FFixture()
		{
			if (!World || !Settings) { return; }
			FCommandLine::Set(TEXT(""));
			Settings->SetSleepUntilEnabled(true);
			Rest = UUEGT2RestSubsystem::Get(World);
			Director = UUEGT2NPCDirector::Get(World);
			Sky = World->SpawnActor<AUEGT2SkyController>();
			AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator);
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!Rest || !Director || !Sky || !Floor || !Cube) { return; }
			Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Floor->SetActorScale3D(FVector(400, 400, 1));
			Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Rest->bFeatureEnabled = true;
			Sky->TimeOfDay = 22.5f;
			Sky->SetDayLengthMinutes(20.0f);
			// Starting only this subsystem avoids launching diagnostic subsystems
			// when a later test temporarily enables a capture command-line gate.
			Director->OnWorldBeginPlay(*World);
			Director->SetCrowdDensity(1.0f);
			Director->SetSchedulesPaused(false);
			Worker = World->SpawnActor<AUEGT2NPCActor>(FVector(10000, 0, 0), FRotator::ZeroRotator);
			Controller = World->SpawnActor<AUEGT2PlayerController>();
			Player = World->SpawnActor<AUEGT2Character>(FVector(0, 0, 92), FRotator::ZeroRotator);
			Bed = AddAmenity(World, EUEGT2AmenityKind::Bed);
			Pauser = World->SpawnActor<APlayerState>();
			if (!Worker || !Controller || !Player || !Bed || !Pauser) { return; }
			Worker->ConfigureNPC(TEXT("Rest test smith"), EUEGT2NPCRole::Smith, EUEGT2NPCSpecies::Person, 4242);
			Worker->AddAnchor(EUEGT2Anchor::Home, FVector(10000, 0, 0));
			Worker->AddAnchor(EUEGT2Anchor::Work, FVector(10200, 0, 0));
			Worker->DispatchBeginPlay();
			Director->Tick(0.0f);
			if (!Director->RestoreCalendar(7, 22.5f, EUEGT2Weather::Clear)) { return; }
			Controller->Possess(Player);
			Player->DispatchBeginPlay();
			Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			FUEGT2NPCNeeds Needs;
			Needs.Energy = 0.22f; Needs.Fed = 0.64f; Needs.Relief = 0.78f; Needs.Company = 0.51f;
			bReady = Player->GetLife()->RestoreProgress(Needs, FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith);
		}

		~FFixture()
		{
			if (Settings) { Settings->SetSleepUntilEnabled(bOriginalSetting); }
			FCommandLine::Set(*OriginalCommandLine);
		}

		static AUEGT2Amenity* AddAmenity(UWorld* InWorld, EUEGT2AmenityKind Kind)
		{
			AUEGT2Amenity* Amenity = InWorld
				? InWorld->SpawnActor<AUEGT2Amenity>(FVector(180, 0, 0), FRotator::ZeroRotator) : nullptr;
			if (Amenity)
			{
				Amenity->ConfigureAmenity(Kind, TEXT("test lodgings"), EUEGT2NPCRole::Villager);
				Amenity->SetUseRange(340.0f);
				Amenity->DispatchBeginPlay();
			}
			return Amenity;
		}
	};

	struct FSnapshot
	{
		FUEGT2NPCNeeds PlayerNeeds;
		FUEGT2Purse PlayerPurse;
		EUEGT2NPCRole Trade;
		EUEGT2Activity PlayerActivity;
		bool bOccupied;
		FVector PlayerLocation;
		FUEGT2NPCNeeds NPCNeeds;
		FUEGT2Purse NPCPurse;
		EUEGT2Activity NPCActivity;
		FVector NPCLocation;
		int32 Day;
		float Hour;
		float SkyHour;
		EUEGT2Weather Weather;

		explicit FSnapshot(const FFixture& Sim)
			: PlayerNeeds(Sim.Player->GetLife()->GetNeeds()), PlayerPurse(Sim.Player->GetLife()->GetPurse()),
			  Trade(Sim.Player->GetLife()->GetTrade()), PlayerActivity(Sim.Player->GetLife()->GetActivity()),
			  bOccupied(Sim.Player->GetLife()->IsOccupied()), PlayerLocation(Sim.Player->GetActorLocation()),
			  NPCNeeds(Sim.Worker->GetNeeds()), NPCPurse(Sim.Worker->GetPurse()),
			  NPCActivity(Sim.Worker->GetActivity()), NPCLocation(Sim.Worker->GetActorLocation()),
			  Day(Sim.Director->GetDayIndex()), Hour(Sim.Director->GetHour()),
			  SkyHour(Sim.Sky->GetTimeOfDay()), Weather(Sim.Sky->GetWeather()) {}
	};

	void CheckUnchanged(FAutomationTestBase& Test, const FFixture& Sim, const FSnapshot& Before, const FString& Label)
	{
		const FSnapshot After(Sim);
		const float Expected[] = { Before.PlayerNeeds.Energy, Before.PlayerNeeds.Fed, Before.PlayerNeeds.Relief,
			Before.PlayerNeeds.Company, Before.PlayerPurse.Coins, Before.NPCNeeds.Energy, Before.NPCNeeds.Fed,
			Before.NPCNeeds.Relief, Before.NPCNeeds.Company, Before.NPCPurse.Coins, Before.Hour, Before.SkyHour };
		const float Actual[] = { After.PlayerNeeds.Energy, After.PlayerNeeds.Fed, After.PlayerNeeds.Relief,
			After.PlayerNeeds.Company, After.PlayerPurse.Coins, After.NPCNeeds.Energy, After.NPCNeeds.Fed,
			After.NPCNeeds.Relief, After.NPCNeeds.Company, After.NPCPurse.Coins, After.Hour, After.SkyHour };
		const TCHAR* Names[] = { TEXT("player energy"), TEXT("player fed"), TEXT("player relief"), TEXT("player company"),
			TEXT("player coins"), TEXT("NPC energy"), TEXT("NPC fed"), TEXT("NPC relief"), TEXT("NPC company"),
			TEXT("NPC coins"), TEXT("director hour"), TEXT("sky hour") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			Test.TestEqual(Label + TEXT(": ") + Names[Index], Actual[Index], Expected[Index]);
		}
		Test.TestEqual(Label + TEXT(": trade"), After.Trade, Before.Trade);
		Test.TestEqual(Label + TEXT(": player activity"), After.PlayerActivity, Before.PlayerActivity);
		Test.TestEqual(Label + TEXT(": venue occupied"), After.bOccupied, Before.bOccupied);
		Test.TestEqual(Label + TEXT(": player location"), After.PlayerLocation, Before.PlayerLocation);
		Test.TestEqual(Label + TEXT(": NPC activity"), After.NPCActivity, Before.NPCActivity);
		Test.TestEqual(Label + TEXT(": NPC location"), After.NPCLocation, Before.NPCLocation);
		Test.TestEqual(Label + TEXT(": day"), After.Day, Before.Day);
		Test.TestEqual(Label + TEXT(": weather"), After.Weather, Before.Weather);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestServicePreviewTest, "UEGT2.Rest.Service.Preview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestServicePreviewTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestServiceTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real rest service fixture is ready"), Sim.bReady)) { return false; }
	const FSnapshot Before(Sim);
	FUEGT2RestPreview Preview;
	FText Reason;
	TestTrue(TEXT("nearby standing player can use the bed"), Sim.Rest->CanSleepAt(Sim.Controller, Sim.Bed, Reason));
	TestTrue(TEXT("default wake time has a valid preview"), Sim.Rest->GetPreview(Sim.Controller, Sim.Bed, 6, Preview, Reason));
	TestTrue(TEXT("successful preview clears the reason"), Reason.IsEmpty());
	TestEqual(TEXT("preview reads the current day"), Preview.StartDayIndex, 7);
	TestEqual(TEXT("preview reads the current hour"), Preview.StartHour, 22.5f);
	TestEqual(TEXT("morning is tomorrow"), Preview.WakeDayIndex, 8);
	TestEqual(TEXT("selected hour preserved"), Preview.WakeHour, 6);
	TestEqual(TEXT("duration includes the fractional starting hour"), Preview.DurationHours, 7.5f);
	TestTrue(TEXT("later tonight is also available"), Sim.Rest->GetPreview(Sim.Controller, Sim.Bed, 23, Preview, Reason));
	TestEqual(TEXT("tonight stays on the same day"), Preview.WakeDayIndex, 7);
	TestEqual(TEXT("tonight lasts half an hour"), Preview.DurationHours, 0.5f);
	CheckUnchanged(*this, Sim, Before, TEXT("preview is read-only"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestServiceEligibilityTest, "UEGT2.Rest.Service.Eligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestServiceEligibilityTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestServiceTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real rest service fixture is ready"), Sim.bReady)) { return false; }
	const FSnapshot Before(Sim);
	FUEGT2RestPreview Preview;
	FText Reason;
	auto Reject = [&](const TCHAR* Label, AUEGT2Amenity* Bed, int32 Hour = 6)
	{
		Reason = FText::GetEmpty();
		TestFalse(Label, Sim.Rest->GetPreview(Sim.Controller, Bed, Hour, Preview, Reason));
		TestFalse(FString(Label) + TEXT(" explains why"), Reason.IsEmpty());
	};
	Reject(TEXT("negative wake hour is rejected"), Sim.Bed, -1);
	Reject(TEXT("hour 24 is rejected rather than folded"), Sim.Bed, 24);
	Reject(TEXT("missing bed is rejected"), nullptr);
	AUEGT2Amenity* Seat = FFixture::AddAmenity(Sim.World, EUEGT2AmenityKind::Seat);
	if (!TestNotNull(TEXT("other amenity spawned"), Seat)) { return false; }
	Reject(TEXT("a seat cannot skip time"), Seat);
	FWorldScope OtherWorld(EWorldType::EditorPreview);
	AUEGT2Amenity* ForeignBed = FFixture::AddAmenity(OtherWorld.World, EUEGT2AmenityKind::Bed);
	if (!TestNotNull(TEXT("bed in a second world spawned"), ForeignBed)) { return false; }
	Reject(TEXT("bed from another world is rejected"), ForeignBed);
	AUEGT2Amenity* DestroyedBed = FFixture::AddAmenity(Sim.World, EUEGT2AmenityKind::Bed);
	if (!TestNotNull(TEXT("temporary bed spawned"), DestroyedBed)) { return false; }
	TestTrue(TEXT("temporary bed is actually destroyed"), DestroyedBed->Destroy());
	Reject(TEXT("destroyed bed is rejected"), DestroyedBed);
	Sim.Bed->SetActorLocation(FVector(1000, 0, 0));
	Reject(TEXT("bed beyond its use range is rejected"), Sim.Bed);
	Sim.Bed->SetActorLocation(FVector(180, 0, 0));
	Sim.Bed->SetUseRange(std::numeric_limits<float>::infinity());
	Reject(TEXT("infinite use range is rejected"), Sim.Bed);
	Sim.Bed->SetUseRange(340.0f);
	Sim.Player->GetLife()->SetCoins(std::numeric_limits<float>::infinity());
	Reject(TEXT("nonfinite player life is rejected"), Sim.Bed);
	Sim.Player->GetLife()->SetCoins(Before.PlayerPurse.Coins);
	Sim.Sky->TimeOfDay = std::numeric_limits<float>::quiet_NaN();
	Reject(TEXT("nonfinite calendar is rejected"), Sim.Bed);
	Sim.Sky->TimeOfDay = Before.SkyHour;
	UCharacterMovementComponent* Movement = Sim.Player->GetCharacterMovement();
	Sim.Player->Crouch();
	Movement->Crouch();
	TestTrue(TEXT("fixture is actually crouched"), Sim.Player->bIsCrouched);
	Reject(TEXT("crouched player is rejected"), Sim.Bed);
	Sim.Player->UnCrouch();
	Movement->UnCrouch();
	Sim.Player->SetActorLocation(Before.PlayerLocation);
	TestFalse(TEXT("fixture stands again"), Sim.Player->bIsCrouched);
	Sim.Player->SetFlyEnabled(true);
	Reject(TEXT("flight is rejected"), Sim.Bed);
	Sim.Player->SetNoclipEnabled(true);
	Reject(TEXT("noclip is rejected"), Sim.Bed);
	Sim.Player->ClearDevMovement();
	Movement->SetMovementMode(MOVE_Falling);
	Reject(TEXT("falling is rejected"), Sim.Bed);
	Movement->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("valid state becomes eligible again"), Sim.Rest->GetPreview(Sim.Controller, Sim.Bed, 6, Preview, Reason));
	CheckUnchanged(*this, Sim, Before, TEXT("rejected previews preserve life and calendar"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestServiceCommitGuardTest, "UEGT2.Rest.Service.CommitRequiresPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestServiceCommitGuardTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestServiceTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real rest service fixture is ready"), Sim.bReady)) { return false; }
	const FSnapshot Before(Sim);
	FText Reason;
	TestFalse(TEXT("world starts unpaused"), Sim.World->IsPaused());
	TestFalse(TEXT("a direct live call cannot skip time"), Sim.Rest->SleepUntil(Sim.Controller, Sim.Bed, 6, Reason));
	TestFalse(TEXT("live rejection explains why"), Reason.IsEmpty());
	CheckUnchanged(*this, Sim, Before, TEXT("live commit rejection"));
	// A real pauser exercises the second guard without constructing a menu.
	Sim.World->GetWorldSettings()->SetPauserPlayerState(Sim.Pauser);
	TestTrue(TEXT("world is actually paused"), Sim.World->IsPaused());
	TestFalse(TEXT("no rest panel was opened"), Sim.Controller->IsRestPanelOpen());
	TestFalse(TEXT("pausing alone cannot authorize a commit"), Sim.Rest->SleepUntil(Sim.Controller, Sim.Bed, 6, Reason));
	TestFalse(TEXT("missing-panel rejection explains why"), Reason.IsEmpty());
	CheckUnchanged(*this, Sim, Before, TEXT("missing-panel commit rejection"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RestServiceOffPathTest, "UEGT2.Rest.Service.DisabledFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RestServiceOffPathTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2RestServiceTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real rest service fixture is ready"), Sim.bReady)) { return false; }
	struct FCase { const TCHAR* Name; bool bPlayer; bool bHard; const TCHAR* CommandLine; };
	const FCase Cases[] = {
		{ TEXT("player switch"), false, true, TEXT("") },
		{ TEXT("maintainer switch"), true, false, TEXT("") },
		{ TEXT("capture gate"), true, true, TEXT("-UEGT2Capture=RestServiceTest") },
		{ TEXT("walk smoke gate"), true, true, TEXT("-UEGT2SmokeWalk") },
		{ TEXT("flight soak gate"), true, true, TEXT("-UEGT2SmokeFly") }
	};
	for (const FCase& Case : Cases)
	{
		const FString Label(Case.Name);
		Sim.Settings->SetSleepUntilEnabled(Case.bPlayer);
		Sim.Rest->bFeatureEnabled = Case.bHard;
		FCommandLine::Set(Case.CommandLine);
		const FSnapshot Before(Sim);
		FUEGT2RestPreview Preview;
		FText Reason;
		TestFalse(Label + TEXT(": service disabled"), Sim.Rest->IsEnabled());
		TestFalse(Label + TEXT(": preview unavailable"), Sim.Rest->GetPreview(Sim.Controller, Sim.Bed, 6, Preview, Reason));
		TestFalse(Label + TEXT(": disabled reason supplied"), Reason.IsEmpty());
		TestFalse(Label + TEXT(": direct commit blocked"), Sim.Rest->SleepUntil(Sim.Controller, Sim.Bed, 6, Reason));
		CheckUnchanged(*this, Sim, Before, Label + TEXT(" before interaction"));
		Sim.Bed->Interact(Sim.Player);
		TestEqual(Label + TEXT(": bed starts ordinary sleep"), Sim.Player->GetLife()->GetActivity(), EUEGT2Activity::Sleep);
		TestTrue(Label + TEXT(": ordinary sleep uses this bed"), Sim.Player->GetLife()->IsUsing(Sim.Bed));
		TestFalse(Label + TEXT(": no rest panel opens"), Sim.Controller->IsRestPanelOpen());
		Sim.Bed->Interact(Sim.Player);
		TestFalse(Label + TEXT(": second interaction gets up"), Sim.Player->GetLife()->IsOccupied());
		CheckUnchanged(*this, Sim, Before, Label + TEXT(" after getting up"));
	}
	Sim.Settings->SetSleepUntilEnabled(true);
	Sim.Rest->bFeatureEnabled = true;
	FCommandLine::Set(TEXT(""));
	FUEGT2RestPreview Preview;
	FText Reason;
	TestTrue(TEXT("reenabling restores preview without resetting life"), Sim.Rest->GetPreview(Sim.Controller, Sim.Bed, 6, Preview, Reason));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
