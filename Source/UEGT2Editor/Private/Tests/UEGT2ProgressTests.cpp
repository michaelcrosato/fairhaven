#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Fixtures/ProgressSchema1.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSave.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Services/UEGT2ServicesSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "World/UEGT2SkyController.h"

#include <limits>

namespace UEGT2ProgressTests
{
	/** Real game-instance service and registered components, isolated from player slots. */
	struct FFixture
	{
		FString OriginalCommandLine = FCommandLine::Get();
		FString Slot = TEXT("UEGT2_ProgressSmoke_Test_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bOriginalSetting = Settings && Settings->GetSaveProgressEnabled();
		TStrongObjectPtr<UGameInstance> Instance;
		UWorld* World = nullptr;
		UUEGT2ProgressSubsystem* Progress = nullptr;
		AUEGT2Character* Player = nullptr;
		AUEGT2PlayerController* Controller = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2NPCActor* Worker = nullptr;
		AUEGT2Landmark* Square = nullptr;
		AUEGT2Landmark* Harbour = nullptr;
		APlayerStart* Start = nullptr;
		APlayerState* Pauser = nullptr;

		FFixture()
		{
			if (!Settings || !GEngine) { return; }
			Settings->SetSaveProgressEnabled(true);
			SetCommandLine();
			Instance.Reset(NewObject<UGameInstance>(GEngine));
			Instance->InitializeStandalone(FName(*(TEXT("ProgressFixture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			World = Instance->GetWorld();
			if (!World) { return; }
			Progress = UUEGT2ProgressSubsystem::Get(World);
			if (!Progress) { return; }
			Progress->bFeatureEnabled = true;
			Sky = World->SpawnActor<AUEGT2SkyController>();
			Director = UUEGT2NPCDirector::Get(World);
			Controller = World->SpawnActor<AUEGT2PlayerController>();
			Player = World->SpawnActor<AUEGT2Character>(FVector(100, 150, 120), FRotator::ZeroRotator);
			Start = World->SpawnActor<APlayerStart>(FVector(1000, 0, 120), FRotator::ZeroRotator);
			Pauser = World->SpawnActor<APlayerState>();
			Square = World->SpawnActor<AUEGT2Landmark>(FVector(3000, 0, 0), FRotator::ZeroRotator);
			Harbour = World->SpawnActor<AUEGT2Landmark>(FVector(4000, 0, 0), FRotator::ZeroRotator);
			Worker = World->SpawnActor<AUEGT2NPCActor>(FVector(10000, 0, 0), FRotator::ZeroRotator);
			if (!Sky || !Director || !Controller || !Player || !Start || !Pauser || !Square || !Harbour || !Worker) { return; }
			Square->PersistentId = TEXT("fairhaven_square");
			Harbour->PersistentId = TEXT("fairhaven_harbour");
			Square->DispatchBeginPlay();
			Harbour->DispatchBeginPlay();
			Sky->SetDayLengthMinutes(4.0f);
			Director->OnWorldBeginPlay(*World);
			Director->SetCrowdDensity(1.0f);
			Director->SetSchedulesPaused(true);
			Worker->ConfigureNPC(TEXT("Checkpoint smith"), EUEGT2NPCRole::Smith, EUEGT2NPCSpecies::Person, 4242);
			Worker->DispatchBeginPlay();
			Director->Tick(0.0f);
			Controller->Possess(Player);
			Player->DispatchBeginPlay();
		}

		~FFixture()
		{
			// These names were generated here, never read from a player argument.
			for (const TCHAR* Suffix : { TEXT("_A"), TEXT("_B") })
			{
				if (UGameplayStatics::DoesSaveGameExist(Slot + Suffix, 0))
				{
					UGameplayStatics::DeleteGameInSlot(Slot + Suffix, 0);
				}
			}
			if (World)
			{
				World->DestroyWorld(false);
			}
			if (Instance.IsValid()) { Instance->Shutdown(); }
			if (World) { GEngine->DestroyWorldContext(World); }
			if (Settings) { Settings->SetSaveProgressEnabled(bOriginalSetting); }
			FCommandLine::Set(*OriginalCommandLine);
		}

		void SetCommandLine(const FString& Extra = FString()) const
		{
			FCommandLine::Set(*FString::Printf(TEXT("-UEGT2ProgressSmoke=Write -UEGT2ProgressSlot=%s %s"), *Slot, *Extra));
		}
		bool Ready() const
		{
			return World && Progress && Player && Player->GetLife()->HasBegunPlay()
				&& Controller && Sky && Director && Worker && Square && Harbour && Start && Pauser;
		}
		void Pause()
		{
			Controller->CloseMenu();
			Controller->ShowPauseMenu();
			// This small world has no game mode. Use the engine's actual pause
			// state, while the packaged smoke covers the full menu/game-mode path.
			World->GetWorldSettings()->SetPauserPlayerState(Pauser);
		}
		void Populate()
		{
			FUEGT2NPCNeeds Needs;
			Needs.Energy = 0.73f; Needs.Fed = 0.42f; Needs.Relief = 0.61f; Needs.Company = 0.28f;
			Player->GetLife()->RestoreProgress(Needs, FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith);
			Director->RestoreCalendar(7, 2.5f, EUEGT2Weather::Cloudy);
			Controller->SetControlRotation(FRotator(-17, 123, 0));
			Square->SetDiscovered(true);
			Harbour->SetDiscovered(false);
		}
		AStaticMeshActor* AddBlocker(const FVector& At)
		{
			AStaticMeshActor* Blocker = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
			if (!Blocker) { return nullptr; }
			Blocker->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Blocker->SetActorScale3D(FVector(2, 2, 4));
			Blocker->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			return Blocker;
		}
	};

	void CheckLife(FAutomationTestBase& Test, const UUEGT2NeedsComponent* Life,
		const FUEGT2NPCNeeds& Needs, float Coins, EUEGT2NPCRole Trade)
	{
		Test.TestEqual(TEXT("energy preserved"), Life->GetNeeds().Energy, Needs.Energy);
		Test.TestEqual(TEXT("fed preserved"), Life->GetNeeds().Fed, Needs.Fed);
		Test.TestEqual(TEXT("relief preserved"), Life->GetNeeds().Relief, Needs.Relief);
		Test.TestEqual(TEXT("company preserved"), Life->GetNeeds().Company, Needs.Company);
		Test.TestEqual(TEXT("fractional coins preserved"), Life->GetPurse().Coins, Coins);
		Test.TestEqual(TEXT("trade preserved"), Life->GetTrade(), Trade);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressRoundTripTest, "UEGT2.Progress.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real checkpoint fixture"), Sim.Ready())) { return false; }
	Sim.Populate();
	const FUEGT2NPCNeeds SavedNeeds = Sim.Player->GetLife()->GetNeeds();
	const FVector SavedLocation = Sim.Player->GetActorLocation();
	const FRotator SavedView = Sim.Controller->GetControlRotation();
	AActor* Venue = Sim.World->SpawnActor<AActor>();
	Sim.Player->GetLife()->BeginActivity(EUEGT2Activity::Work, Venue, FText::FromString(TEXT("Test smithy")), EUEGT2NPCRole::Smith, 400);
	Sim.Pause();
	TestTrue(TEXT("manual checkpoint saves while paused"), Sim.Progress->SaveProgress(Sim.Controller));
	TestTrue(TEXT("valid checkpoint available"), Sim.Progress->HasSavedProgress());

	// A new pawn really runs BeginPlay and seeds its own purse before loading.
	AUEGT2Character* OriginalPlayer = Sim.Player;
	Sim.Controller->UnPossess();
	OriginalPlayer->Destroy();
	Sim.Player = Sim.World->SpawnActor<AUEGT2Character>(FVector(2000, 1000, 250), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("replacement player"), Sim.Player)) { return false; }
	Sim.Player->DispatchBeginPlay();
	Sim.Controller->Possess(Sim.Player);
	Sim.Player->SetNoclipEnabled(true);
	Sim.Player->GetCharacterMovement()->Velocity = FVector(150, 30, 900);
	Sim.Player->GetLife()->SetConversing(true);
	Sim.Director->RestoreCalendar(30, 23.75f, EUEGT2Weather::Storm);
	Sim.Square->SetDiscovered(false);
	Sim.Harbour->SetDiscovered(true);
	TestTrue(TEXT("explicit load succeeds after BeginPlay"), Sim.Progress->LoadProgress(Sim.Controller));
	CheckLife(*this, Sim.Player->GetLife(), SavedNeeds, 137.625f, EUEGT2NPCRole::Smith);
	TestTrue(TEXT("position restored"), Sim.Player->GetActorLocation().Equals(SavedLocation, 0.01));
	TestTrue(TEXT("view restored"), Sim.Controller->GetControlRotation().Equals(SavedView, 0.01));
	TestTrue(TEXT("velocity cleared"), Sim.Player->GetVelocity().IsNearlyZero());
	TestFalse(TEXT("flight cleared"), Sim.Player->IsFlyEnabled());
	TestFalse(TEXT("noclip cleared"), Sim.Player->IsNoclipEnabled());
	TestFalse(TEXT("venue not restored"), Sim.Player->GetLife()->IsOccupied());
	TestEqual(TEXT("resumes idle"), Sim.Player->GetLife()->GetActivity(), EUEGT2Activity::Idle);
	TestEqual(TEXT("calendar restored"), Sim.Director->GetDayIndex(), 7);
	TestEqual(TEXT("sky restored"), Sim.Sky->GetTimeOfDay(), 2.5f);
	TestEqual(TEXT("weather restored"), Sim.Sky->GetWeather(), EUEGT2Weather::Cloudy);
	TestTrue(TEXT("survey restored"), Sim.Square->IsDiscovered());
	TestFalse(TEXT("unsurveyed place restored"), Sim.Harbour->IsDiscovered());
	TestTrue(TEXT("repeat load succeeds"), Sim.Progress->LoadProgress(Sim.Controller));
	TestEqual(TEXT("repeat load never duplicates surveys"), AUEGT2Landmark::GetDiscoveredCount(Sim.World), 1);
	Sim.Director->Tick(0.0f);
	TestEqual(TEXT("backward load is not a midnight crossing"), Sim.Director->GetDayIndex(), 7);
	FUEGT2NPCNeeds Expected = SavedNeeds;
	FUEGT2Purse ExpectedPurse(137.625f);
	UEGT2AdvanceLife(0.1f, EUEGT2Activity::Idle, EUEGT2NPCRole::Smith, Expected, ExpectedPurse);
	Sim.Player->GetLife()->TickComponent(1.0f, LEVELTICK_All, &Sim.Player->GetLife()->PrimaryComponentTick);
	CheckLife(*this, Sim.Player->GetLife(), Expected, ExpectedPurse.Coins, EUEGT2NPCRole::Smith);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressAutoWalkTest, "UEGT2.Progress.AutoWalkRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressAutoWalkTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real checkpoint fixture"), Sim.Ready())) { return false; }
	const bool bOriginalAutoWalk = Sim.Settings->GetAutoWalkEnabled();
	// Auto-walk belongs to a local human controller. Other checkpoint tests
	// need no local input stack, so attach one only for this integration case.
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
	LocalPlayer->PlayerAdded(nullptr, 0);
	Sim.Controller->SetPlayer(LocalPlayer.Get());
	ON_SCOPE_EXIT
	{
		Sim.Controller->UnPossess();
		LocalPlayer->PlayerRemoved();
		Sim.Settings->SetAutoWalkEnabled(bOriginalAutoWalk);
	};
	if (!TestTrue(TEXT("checkpoint fixture has a local controller"), Sim.Controller->IsLocalController())) { return false; }
	Sim.Settings->SetAutoWalkEnabled(true);
	Sim.Player->bAutoWalkFeatureEnabled = true;
	Sim.Populate();
	Sim.Controller->CloseMenu();
	Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("assistance starts before opening Save Progress"), Sim.Player->ToggleAutoWalk());
	Sim.Player->ApplyAutoWalkInput();
	TestFalse(TEXT("assistance queued ordinary input"), Sim.Player->GetPendingMovementInputVector().IsNearlyZero());
	Sim.Pause();
	TestFalse(TEXT("actual pause path cancels assistance"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("pause clears queued assistance"), Sim.Player->GetPendingMovementInputVector().IsNearlyZero());
	TestTrue(TEXT("checkpoint saves after auto-walk stops"), Sim.Progress->SaveProgress(Sim.Controller));
	Sim.Controller->CloseMenu();
	Sim.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	TestFalse(TEXT("closing menu does not resume assistance"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("explicit toggle can start another walk"), Sim.Player->ToggleAutoWalk());
	Sim.Player->ApplyAutoWalkInput();
	// The shared restore service must clear movement even when its caller did
	// not first open a menu. Both checkpoint channels use this restore path.
	TestTrue(TEXT("checkpoint loads while assistance is active"), Sim.Progress->LoadProgress(Sim.Controller));
	TestFalse(TEXT("restoration clears transient assistance"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("restoration clears queued input"), Sim.Player->GetPendingMovementInputVector().IsNearlyZero());
	TestTrue(TEXT("restoration clears velocity"), Sim.Player->GetVelocity().IsNearlyZero());
	Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Sim.Player->ApplyAutoWalkInput();
	TestFalse(TEXT("landing after restoration does not resume"), Sim.Player->IsAutoWalking());
	TestTrue(TEXT("landing adds no movement"), Sim.Player->GetPendingMovementInputVector().IsNearlyZero());
	TestTrue(TEXT("restoration retains player opt-in"), Sim.Settings->GetAutoWalkEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressServiceTrackingTest, "UEGT2.Progress.ServiceTrackingRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressServiceTrackingTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real checkpoint fixture"), Sim.Ready())) { return false; }
	UUEGT2ServicesSubsystem* Services = UUEGT2ServicesSubsystem::Get(Sim.World);
	if (!TestNotNull(TEXT("world service guide"), Services)) { return false; }
	const bool bOriginalServices = Sim.Settings->GetNearbyServicesEnabled();
	ON_SCOPE_EXIT { Sim.Settings->SetNearbyServicesEnabled(bOriginalServices); };
	Sim.Settings->SetNearbyServicesEnabled(true);
	Services->bFeatureEnabled = true;
	AUEGT2Amenity* Kitchen = Sim.World->SpawnActor<AUEGT2Amenity>(FVector(5000, 0, 0), FRotator::ZeroRotator);
	AUEGT2Amenity* Seat = Sim.World->SpawnActor<AUEGT2Amenity>(FVector(6000, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("kitchen"), Kitchen) || !TestNotNull(TEXT("seat"), Seat)) { return false; }
	Kitchen->ConfigureAmenity(EUEGT2AmenityKind::Larder, TEXT("Test lodgings"), EUEGT2NPCRole::Villager);
	Seat->ConfigureAmenity(EUEGT2AmenityKind::Seat, FString(), EUEGT2NPCRole::Villager);
	Kitchen->DispatchBeginPlay();
	Seat->DispatchBeginPlay();
	Sim.Populate();
	const FUEGT2NPCNeeds SavedNeeds = Sim.Player->GetLife()->GetNeeds();
	TestTrue(TEXT("track kitchen before saving"), Services->TrackAmenity(Kitchen));
	Sim.Pause();
	TestTrue(TEXT("checkpoint with directions saves"), Sim.Progress->SaveProgress(Sim.Controller));
	TestTrue(TEXT("choose a different live target after saving"), Services->TrackAmenity(Seat));
	TestTrue(TEXT("same-world Continue succeeds"), Sim.Progress->LoadProgress(Sim.Controller));
	TestTrue(TEXT("Continue keeps current seat, not previously tracked kitchen"), Services->GetTrackedAmenity() == Seat);
	CheckLife(*this, Sim.Player->GetLife(), SavedNeeds, 137.625f, EUEGT2NPCRole::Smith);
	Services->ClearTracking();
	TestTrue(TEXT("Continue succeeds after clearing directions"), Sim.Progress->LoadProgress(Sim.Controller));
	TestNull(TEXT("loading cannot resurrect a saved target"), Services->GetTrackedAmenity());
	TestTrue(TEXT("can choose kitchen again"), Services->TrackAmenity(Kitchen));
	Sim.Settings->SetSaveProgressEnabled(false);
	TestTrue(TEXT("guide remains enabled with checkpoint feature off"), Services->IsEnabled());
	TestTrue(TEXT("tracking remains independent of checkpoint preference"), Services->TrackAmenity(Seat));
	Sim.Settings->SetSaveProgressEnabled(true);
	// Only the fixture's generated slot is damaged. Failed restore must leave
	// the current directions and life alone, just as it leaves position alone.
	const TArray<uint8> InvalidBytes = { 0xff, 0xff, 0xff, 0x7f };
	TestTrue(TEXT("replace isolated checkpoint with damaged bytes"), UGameplayStatics::SaveDataToSlot(InvalidBytes, Sim.Slot + TEXT("_A"), 0));
	Sim.Player->GetLife()->SetCoins(55.25f);
	TestFalse(TEXT("damaged checkpoint refuses restore"), Sim.Progress->LoadProgress(Sim.Controller));
	TestTrue(TEXT("failed restore keeps current directions"), Services->GetTrackedAmenity() == Seat);
	CheckLife(*this, Sim.Player->GetLife(), SavedNeeds, 55.25f, EUEGT2NPCRole::Smith);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressValidationTest, "UEGT2.Progress.ValidationAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressValidationTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("checkpoint fixture"), Sim.Ready())) { return false; }
	Sim.Populate(); Sim.Pause();
	TestTrue(TEXT("initial checkpoint"), Sim.Progress->SaveProgress(Sim.Controller));
	TArray<uint8> Encoded;
	FText Reason;
	UGameplayStatics::LoadDataFromSlot(Encoded, Sim.Slot + TEXT("_A"), 0);
	TStrongObjectPtr<UUEGT2ProgressSave> Saved(UUEGT2ProgressSave::Decode(Encoded, Reason));
	if (!TestTrue(TEXT("real serialized payload"), Saved.IsValid())) { return false; }
	TSet<FName> Ids = { TEXT("fairhaven_square"), TEXT("fairhaven_harbour") };
	TestTrue(TEXT("complete valid payload"), Saved->Validate(Saved->MapPackageName, Ids, Reason));
	using FDamage = TFunction<void(UUEGT2ProgressSave*)>;
	const TArray<FDamage> Damage = {
		[](auto* Save) { ++Save->SchemaVersion; },
		[](auto* Save) { ++Save->ContentRevision; },
		[](auto* Save) { Save->MapPackageName = TEXT("/Game/Maps/Other"); },
		[](auto* Save) { Save->Needs.Fed = std::numeric_limits<float>::quiet_NaN(); },
		[](auto* Save) { Save->Purse.Coins = std::numeric_limits<float>::infinity(); },
		[](auto* Save) { Save->Purse.Coins = -1.0f; },
		[](auto* Save) { Save->Trade = EUEGT2NPCRole::Count; },
		[](auto* Save) { Save->PlayerLocation.X = std::numeric_limits<double>::infinity(); },
		[](auto* Save) { Save->ViewRotation.Yaw = std::numeric_limits<double>::quiet_NaN(); },
		[](auto* Save) { Save->DayIndex = -1; },
		[](auto* Save) { Save->Hour = 24.0f; },
		[](auto* Save) { Save->Weather = EUEGT2Weather::Count; },
		[](auto* Save) { Save->DiscoveredLandmarks.Add(TEXT("unknown_place")); },
		[](auto* Save) { const FName Duplicate = Save->DiscoveredLandmarks[0]; Save->DiscoveredLandmarks.Add(Duplicate); }
	};
	for (int32 Index = 0; Index < Damage.Num(); ++Index)
	{
		TStrongObjectPtr<UUEGT2ProgressSave> Invalid(DuplicateObject<UUEGT2ProgressSave>(Saved.Get(), GetTransientPackage()));
		Damage[Index](Invalid.Get());
		TestFalse(FString::Printf(TEXT("invalid field %d rejects entire payload"), Index), Invalid->Validate(Saved->MapPackageName, Ids, Reason));
		TestFalse(TEXT("invalid payload has a readable reason"), Reason.IsEmpty());
	}
	const FUEGT2NPCNeeds OriginalNeeds = Sim.Player->GetLife()->GetNeeds();
	FUEGT2NPCNeeds InvalidNeeds = OriginalNeeds;
	InvalidNeeds.Energy = -0.5f;
	TestFalse(TEXT("component refuses invalid restoration"), Sim.Player->GetLife()->RestoreProgress(InvalidNeeds, FUEGT2Purse(9), EUEGT2NPCRole::Baker));
	CheckLife(*this, Sim.Player->GetLife(), OriginalNeeds, 137.625f, EUEGT2NPCRole::Smith);
	TArray<uint8> Before;
	UGameplayStatics::LoadDataFromSlot(Before, Sim.Slot + TEXT("_A"), 0);
	Sim.Player->GetLife()->SetCoins(std::numeric_limits<float>::infinity());
	TestFalse(TEXT("invalid live state cannot replace checkpoint"), Sim.Progress->SaveProgress(Sim.Controller));
	TArray<uint8> After;
	UGameplayStatics::LoadDataFromSlot(After, Sim.Slot + TEXT("_A"), 0);
	TestTrue(TEXT("previous valid checkpoint is byte-for-byte unchanged"), Before == After);
	TestTrue(TEXT("load recovers valid data"), Sim.Progress->LoadProgress(Sim.Controller));
	Sim.Player->GetLife()->SetCoins(200.5f);
	TestTrue(TEXT("second checkpoint uses other file"), Sim.Progress->SaveProgress(Sim.Controller));
	TestTrue(TEXT("rotating backup exists"), UGameplayStatics::DoesSaveGameExist(Sim.Slot + TEXT("_B"), 0));
	// All of these must be rejected before UE reads any FString/array length.
	TArray<uint8> Truncated = Encoded;
	Truncated.SetNum(Truncated.Num() - 5);
	TArray<uint8> BitFlip = Encoded;
	BitFlip[BitFlip.Num() - 1] ^= 0x40;
	TArray<uint8> BadLength = Encoded;
	BadLength[8] ^= 0x01;
	const TArray<TArray<uint8>> Damaged = { TArray<uint8>{ 0xff, 0xff, 0xff, 0x7f },
		Truncated, BitFlip, BadLength };
	for (const TArray<uint8>& Bytes : Damaged)
	{
		TestNull(TEXT("damaged envelope is rejected before generic decoding"), UUEGT2ProgressSave::Decode(Bytes, Reason));
		TestTrue(TEXT("simulated damaged newest write"), UGameplayStatics::SaveDataToSlot(Bytes, Sim.Slot + TEXT("_B"), 0));
		TestTrue(TEXT("damaged newest file falls back"), Sim.Progress->LoadProgress(Sim.Controller));
		TestEqual(TEXT("fallback is the prior complete checkpoint"), Sim.Player->GetLife()->GetPurse().Coins, 137.625f);
	}
	Saved->DiscoveredLandmarks.Add(TEXT("missing_landmark"));
	Saved->Encode(Encoded);
	UGameplayStatics::SaveDataToSlot(Encoded, Sim.Slot + TEXT("_A"), 0);
	const FVector BeforeLocation = Sim.Player->GetActorLocation();
	Sim.Player->GetLife()->SetCoins(55.25f);
	TestFalse(TEXT("no compatible snapshot leaves live state alone"), Sim.Progress->LoadProgress(Sim.Controller));
	TestEqual(TEXT("failed load does not change purse"), Sim.Player->GetLife()->GetPurse().Coins, 55.25f);
	TestTrue(TEXT("failed load does not move player"), Sim.Player->GetActorLocation().Equals(BeforeLocation));
	Sim.Progress->SetJourneyActive(false); // a menu rebuild must explain an existing bad save
	TestFalse(TEXT("no valid checkpoint is offered"), Sim.Progress->HasSavedProgress());
	TestFalse(TEXT("unavailable existing checkpoint has visible status"), Sim.Progress->GetStatusText().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressDisabledTest, "UEGT2.Progress.Disabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressDisabledTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("checkpoint fixture"), Sim.Ready())) { return false; }
	Sim.Populate(); Sim.Pause();
	TestTrue(TEXT("checkpoint created before disabling"), Sim.Progress->SaveProgress(Sim.Controller));
	TArray<uint8> Original;
	UGameplayStatics::LoadDataFromSlot(Original, Sim.Slot + TEXT("_A"), 0);
	Sim.Player->GetLife()->SetCoins(23.5f);
	const auto CheckOff = [&]()
	{
		TestFalse(TEXT("effective feature gate is off"), Sim.Progress->IsEnabled());
		TestFalse(TEXT("disabled checkpoint availability"), Sim.Progress->HasSavedProgress());
		TestFalse(TEXT("disabled save refused"), Sim.Progress->SaveProgress(Sim.Controller));
		TestFalse(TEXT("disabled load refused"), Sim.Progress->LoadProgress(Sim.Controller));
		TestEqual(TEXT("disabled load leaves progression untouched"), Sim.Player->GetLife()->GetPurse().Coins, 23.5f);
		TArray<uint8> Current;
		UGameplayStatics::LoadDataFromSlot(Current, Sim.Slot + TEXT("_A"), 0);
		TestTrue(TEXT("disabled operation preserves existing file"), Original == Current);
		TestFalse(TEXT("disabled operation creates no alternate file"), UGameplayStatics::DoesSaveGameExist(Sim.Slot + TEXT("_B"), 0));
	};
	Sim.Settings->SetSaveProgressEnabled(false);
	TestTrue(TEXT("player can still turn the preference back on"), Sim.Progress->IsAvailable());
	CheckOff();
	Sim.Settings->SetSaveProgressEnabled(true);
	Sim.Progress->bFeatureEnabled = false;
	TestFalse(TEXT("hard gate cannot be overridden by player"), Sim.Progress->IsAvailable());
	CheckOff();
	Sim.Progress->bFeatureEnabled = true;
	for (const TCHAR* Switch : { TEXT("-UEGT2Capture=TownSquare"), TEXT("-UEGT2CaptureLife"),
		TEXT("-UEGT2SmokeWalk"), TEXT("-UEGT2SmokeFly"), TEXT("-UEGT2HudSizeSmoke"),
		TEXT("-UEGT2AutoWalkSmoke"), TEXT("-UEGT2ServicesSmoke") })
	{
		Sim.SetCommandLine(Switch);
		CheckOff();
	}
	for (const TCHAR* Unsafe : { TEXT("-UEGT2ProgressSlot=Fairhaven_Journey"),
		TEXT("-UEGT2ProgressSlot"), TEXT("-UEGT2ProgressSmoke"),
		TEXT("-UEGT2ContractSmoke"), TEXT("-UEGT2ContractSlot"),
		TEXT("-UEGT2ContractSmoke=Write"), TEXT("-UEGT2ContractSlot=Fairhaven_Journey"),
		TEXT("-UEGT2ContractSmoke=Write -UEGT2ContractSlot=UEGT2_ContractSmoke_bad"),
		TEXT("-UEGT2ProgressSlot="), TEXT("-UEGT2ProgressSmoke="),
		TEXT("-UEGT2ProgressSmoke=\"\" -UEGT2ProgressSlot=\"\""),
		TEXT("-UEGT2ProgressSmoke=Write -UEGT2ProgressSlot=Fairhaven_Journey"),
		TEXT("-UEGT2ProgressSmoke=Write -UEGT2ProgressSlot=UEGT2_ProgressSmoke_../escape"),
		TEXT("-UEGT2ProgressSmoke=Write"), TEXT("-UEGT2ProgressSmoke=Unknown -UEGT2ProgressSlot=UEGT2_ProgressSmoke_Test") })
	{
		FCommandLine::Set(Unsafe);
		CheckOff();
	}
	Sim.SetCommandLine();
	TestTrue(TEXT("re-enabling exposes the preserved checkpoint"), Sim.Progress->HasSavedProgress());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressPlacementTest, "UEGT2.Progress.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressPlacementTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("checkpoint fixture"), Sim.Ready())) { return false; }
	Sim.Populate(); Sim.Pause();
	const FVector SavedLocation = Sim.Player->GetActorLocation();
	Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Sim.Player->Crouch();
	Sim.Player->GetCharacterMovement()->Crouch();
	TestTrue(TEXT("player actually crouched"), Sim.Player->bIsCrouched);
	TestTrue(TEXT("save standing-equivalent position while crouched"), Sim.Progress->SaveProgress(Sim.Controller));
	Sim.Player->TeleportTo(FVector(2000, 1000, 120), FRotator::ZeroRotator, false, true);
	TestTrue(TEXT("load standing from a crouched pawn"), Sim.Progress->LoadProgress(Sim.Controller));
	TestFalse(TEXT("restore stands up"), Sim.Player->bIsCrouched);
	TestTrue(TEXT("uncrouching does not raise the standing center twice"), Sim.Player->GetActorLocation().Equals(SavedLocation, 0.01));
	Sim.Player->TeleportTo(FVector(2000, 1000, 120), FRotator::ZeroRotator, false, true);
	if (!TestNotNull(TEXT("real collision at saved location"), Sim.AddBlocker(SavedLocation))) { return false; }
	Sim.Player->SetNoclipEnabled(true);
	TestTrue(TEXT("blocked saved position falls back despite noclip"), Sim.Progress->LoadProgress(Sim.Controller));
	TestTrue(TEXT("standing capsule placed at clear PlayerStart"), Sim.Player->GetActorLocation().Equals(Sim.Start->GetActorLocation(), 0.01));
	TestTrue(TEXT("fallback is visible in status"), Sim.Progress->GetStatusText().ToString().Contains(TEXT("blocked")));
	Sim.Player->TeleportTo(FVector(2000, 1000, 120), FRotator::ZeroRotator, false, true);
	if (!TestNotNull(TEXT("real collision at fallback"), Sim.AddBlocker(Sim.Start->GetActorLocation()))) { return false; }
	Sim.Player->GetLife()->SetCoins(44.5f);
	Sim.Director->RestoreCalendar(20, 19.0f, EUEGT2Weather::Clear);
	const FVector Before = Sim.Player->GetActorLocation();
	TestFalse(TEXT("no safe location refuses full restoration"), Sim.Progress->LoadProgress(Sim.Controller));
	TestTrue(TEXT("failed placement does not teleport"), Sim.Player->GetActorLocation().Equals(Before));
	TestEqual(TEXT("failed placement does not restore coins"), Sim.Player->GetLife()->GetPurse().Coins, 44.5f);
	TestEqual(TEXT("failed placement does not restore calendar"), Sim.Director->GetDayIndex(), 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressLifecycleTest, "UEGT2.Progress.LifecycleAndCalendar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("checkpoint fixture"), Sim.Ready())) { return false; }
	Sim.Populate();
	Sim.Controller->ShowMainMenu();
	TestFalse(TEXT("front end cannot save background state"), Sim.Progress->SaveProgress(Sim.Controller));
	Sim.Controller->CloseMenu();
	TestFalse(TEXT("active gameplay must pause before synchronous save"), Sim.Progress->SaveProgress(Sim.Controller));
	Sim.Pause();
	TestTrue(TEXT("manual checkpoint saved"), Sim.Progress->SaveProgress(Sim.Controller));
	TArray<uint8> Before;
	UGameplayStatics::LoadDataFromSlot(Before, Sim.Slot + TEXT("_A"), 0);
	Sim.Progress->RequestNewJourney();
	TestFalse(TEXT("new-journey transition is not active gameplay"), Sim.Progress->SaveProgress(Sim.Controller));
	TestTrue(TEXT("new journey intent survives until consumed"), Sim.Progress->ConsumeNewJourneyRequest());
	TestFalse(TEXT("new journey intent consumed once"), Sim.Progress->ConsumeNewJourneyRequest());
	TArray<uint8> After;
	UGameplayStatics::LoadDataFromSlot(After, Sim.Slot + TEXT("_A"), 0);
	TestTrue(TEXT("new journey request preserves old checkpoint"), Before == After);

	const FUEGT2NPCNeeds WorkerNeeds = Sim.Worker->GetNeeds();
	const float WorkerCoins = Sim.Worker->GetPurse().Coins;
	TestTrue(TEXT("calendar accepts valid checkpoint"), Sim.Director->RestoreCalendar(10, 23.9f, EUEGT2Weather::Storm));
	Sim.Director->Tick(0.0f);
	TestTrue(TEXT("calendar restores large backward jump"), Sim.Director->RestoreCalendar(3, 0.1f, EUEGT2Weather::Clear));
	Sim.Director->Tick(0.0f);
	TestEqual(TEXT("restore does not invent a midnight"), Sim.Director->GetDayIndex(), 3);
	TestEqual(TEXT("calendar jump earns no NPC wages"), Sim.Worker->GetPurse().Coins, WorkerCoins);
	TestEqual(TEXT("calendar jump drains no NPC needs"), Sim.Worker->GetNeeds().Energy, WorkerNeeds.Energy);
	TestFalse(TEXT("invalid calendar has no partial effect"), Sim.Director->RestoreCalendar(9, std::numeric_limits<float>::quiet_NaN(), EUEGT2Weather::Storm));
	TestEqual(TEXT("invalid calendar preserves day"), Sim.Director->GetDayIndex(), 3);
	TestEqual(TEXT("invalid calendar preserves sky"), Sim.Sky->GetTimeOfDay(), 0.1f);
	Sim.Sky->SetTimeOfDay(23.9f); Sim.Director->Tick(0.0f);
	Sim.Sky->SetTimeOfDay(0.1f); Sim.Director->Tick(0.0f);
	TestEqual(TEXT("real subsequent midnight still advances day"), Sim.Director->GetDayIndex(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressSchemaMigrationTest, "UEGT2.Progress.LegacySchemaMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressSchemaMigrationTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Bytes;
	TestTrue(TEXT("genuine old packaged bytes decode from text"), FBase64::Decode(UEGT2ProgressFixtures::Schema1Base64, Bytes));
	TestEqual(TEXT("unmodified old envelope size"), Bytes.Num(), 3088);
	FText Reason;
	TStrongObjectPtr<UUEGT2ProgressSave> Legacy(UUEGT2ProgressSave::Decode(Bytes, Reason));
	if (!TestTrue(TEXT("actual absent-version schema-1 payload migrates"), Legacy.IsValid())) { return false; }
	const TSet<FName> Known = { TEXT("fairhaven_square"), TEXT("fairhaven_harbour"), TEXT("fairhaven_light"), TEXT("mill_rise") };
	TestEqual(TEXT("legacy is upgraded in memory"), Legacy->SchemaVersion, UUEGT2ProgressSave::CurrentSchemaVersion);
	TestTrue(TEXT("same content and landmark identities remain compatible"), Legacy->Validate(TEXT("/Game/Maps/L_Fairhaven"), Known, Reason));
	TestFalse(TEXT("legacy contract starts unpaid"), Legacy->bTownSurveyContractPaid);
	TestEqual(TEXT("legacy fractional purse"), Legacy->Purse.Coins, 137.625f);
	TestEqual(TEXT("legacy energy"), Legacy->Needs.Energy, 0.73f);
	TestEqual(TEXT("legacy fed"), Legacy->Needs.Fed, 0.42f);
	TestEqual(TEXT("legacy relief"), Legacy->Needs.Relief, 0.61f);
	TestEqual(TEXT("legacy company"), Legacy->Needs.Company, 0.28f);
	TestEqual(TEXT("legacy trade"), Legacy->Trade, EUEGT2NPCRole::Smith);
	TestEqual(TEXT("legacy day"), Legacy->DayIndex, 7);
	TestEqual(TEXT("legacy hour"), Legacy->Hour, 13.25f);
	TestEqual(TEXT("legacy weather"), Legacy->Weather, EUEGT2Weather::Cloudy);
	TestEqual(TEXT("legacy discovery count"), Legacy->DiscoveredLandmarks.Num(), 1);
	TestTrue(TEXT("legacy surveyed square"), Legacy->DiscoveredLandmarks.Contains(TEXT("fairhaven_square")));
	Legacy->bTownSurveyContractPaid = true;
	TestFalse(TEXT("payment requires discoveries in the same checkpoint"), Legacy->Validate(Legacy->MapPackageName, Known, Reason));
	for (FName Id : UUEGT2SurveyContractSubsystem::RequiredLandmarkIds()) { Legacy->DiscoveredLandmarks.Add(Id); }
	TestTrue(TEXT("paid schema-2 state is valid"), Legacy->Validate(Legacy->MapPackageName, Known, Reason));
	TestTrue(TEXT("new payload encodes"), Legacy->Encode(Bytes));
	TStrongObjectPtr<UUEGT2ProgressSave> Current(UUEGT2ProgressSave::Decode(Bytes, Reason));
	if (!TestTrue(TEXT("schema-2 payload decodes"), Current.IsValid())) { return false; }
	TestTrue(TEXT("explicit schema prevents mistakenly migrating current paid data"), Current->bTownSurveyContractPaid);
	TestEqual(TEXT("roundtrip schema"), Current->SchemaVersion, UUEGT2ProgressSave::CurrentSchemaVersion);
	for (int32 Unknown : { 0, -1, UUEGT2ProgressSave::CurrentSchemaVersion + 1 })
	{
		Current->SchemaVersion = Unknown;
		TestTrue(TEXT("test unknown-version envelope"), Current->Encode(Bytes));
		TStrongObjectPtr<UUEGT2ProgressSave> Future(UUEGT2ProgressSave::Decode(Bytes, Reason));
		TestTrue(TEXT("unknown-version bytes can be read for validation"), Future.IsValid());
		if (Future.IsValid()) { TestFalse(TEXT("unknown schema cannot be used"), Future->Validate(Current->MapPackageName, Known, Reason)); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ProgressContractTest, "UEGT2.Progress.ContractRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ProgressContractTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ProgressTests;
	FFixture Sim;
	if (!TestTrue(TEXT("checkpoint fixture"), Sim.Ready())) { return false; }
	UUEGT2SurveyContractSubsystem* Contract = UUEGT2SurveyContractSubsystem::Get(Sim.World);
	if (!TestNotNull(TEXT("durable contract service"), Contract)) { return false; }
	for (FName Id : { FName(TEXT("fairhaven_light")), FName(TEXT("mill_rise")) })
	{
		AUEGT2Landmark* Place = Sim.World->SpawnActor<AUEGT2Landmark>();
		if (!TestNotNull(TEXT("required place"), Place)) { return false; }
		Place->PersistentId = Id;
		Place->SetDiscovered(true);
	}
	Sim.Populate(); Sim.Pause();
	TestFalse(TEXT("new world unpaid"), Contract->IsPaid());
	TestTrue(TEXT("prepayment checkpoint A"), Sim.Progress->SaveProgress(Sim.Controller));
	Sim.Harbour->SetDiscovered(true);
	Contract->RestorePaidState(true);
	Sim.Player->GetLife()->SetCoins(155.625f);
	Contract->bFeatureEnabled = false;
	TestTrue(TEXT("paid checkpoint B captured even with contract off"), Sim.Progress->SaveProgress(Sim.Controller));
	Contract->RestorePaidState(false);
	Sim.Player->GetLife()->SetCoins(8.25f);
	int32 RestoreNotifications = 0;
	const FDelegateHandle Restored = Sim.Player->GetLife()->OnActivityChanged.AddLambda([&](EUEGT2Activity, const FText&)
	{
		++RestoreNotifications;
		TestTrue(TEXT("restore observer sees matching paid state"), Contract->IsPaid());
		TestEqual(TEXT("restore observer sees matching purse"), Sim.Player->GetLife()->GetPurse().Coins, 155.625f);
		TestTrue(TEXT("restore observer sees matching discoveries"), Sim.Harbour->IsDiscovered());
	});
	TestTrue(TEXT("paid checkpoint restores with contract off"), Sim.Progress->LoadProgress(Sim.Controller));
	Sim.Player->GetLife()->OnActivityChanged.Remove(Restored);
	TestEqual(TEXT("one coherent restored-activity notification"), RestoreNotifications, 1);
	TestTrue(TEXT("paid flag restored while off"), Contract->IsPaid());
	TestEqual(TEXT("payment restored without a second reward"), Sim.Player->GetLife()->GetPurse().Coins, 155.625f);
	TestTrue(TEXT("repeat restore idempotent"), Sim.Progress->LoadProgress(Sim.Controller));
	TestEqual(TEXT("repeat restore exact balance"), Sim.Player->GetLife()->GetPurse().Coins, 155.625f);
	TArray<uint8> PaidBytes;
	UGameplayStatics::LoadDataFromSlot(PaidBytes, Sim.Slot + TEXT("_B"), 0);
	const TArray<uint8> Damaged = { 0xff, 0x00 };
	UGameplayStatics::SaveDataToSlot(Damaged, Sim.Slot + TEXT("_B"), 0);
	TestTrue(TEXT("corrupt newest falls back to prepayment"), Sim.Progress->LoadProgress(Sim.Controller));
	TestFalse(TEXT("fallback rolls back paid flag"), Contract->IsPaid());
	TestEqual(TEXT("fallback also rolls back payment"), Sim.Player->GetLife()->GetPurse().Coins, 137.625f);
	UGameplayStatics::SaveDataToSlot(Damaged, Sim.Slot + TEXT("_A"), 0);
	Contract->RestorePaidState(true);
	Sim.Player->GetLife()->SetCoins(55.25f);
	TestFalse(TEXT("all damaged checkpoint load fails"), Sim.Progress->LoadProgress(Sim.Controller));
	TestTrue(TEXT("failed load retains paid state"), Contract->IsPaid());
	TestEqual(TEXT("failed load retains exact balance"), Sim.Player->GetLife()->GetPurse().Coins, 55.25f);
	UGameplayStatics::SaveDataToSlot(PaidBytes, Sim.Slot + TEXT("_B"), 0);
	Contract->bFeatureEnabled = true;
	TestTrue(TEXT("re-enabled contract uses preserved paid checkpoint"), Sim.Progress->LoadProgress(Sim.Controller));
	TestTrue(TEXT("re-enable never reopens payment"), Contract->IsPaid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ContractSlotIsolationTest, "UEGT2.Progress.ContractSlotIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ContractSlotIsolationTest::RunTest(const FString& Parameters)
{
	UEGT2ProgressTests::FFixture Sim;
	if (!TestTrue(TEXT("checkpoint fixture"), Sim.Ready())) { return false; }
	const FString RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Slot = TEXT("UEGT2_ContractSmoke_") + RunId;
	FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/ContractSmoke"), RunId));
	FPaths::NormalizeDirectoryName(Directory);
	const FString Valid = FString::Printf(TEXT("-UEGT2ContractSmoke=Write -UEGT2ContractSlot=%s -UserDir=\"%s\""), *Slot, *Directory);
	for (const TCHAR* Mode : { TEXT("Write"), TEXT("Read"), TEXT("NewVisit"), TEXT("Disabled") })
	{
		FCommandLine::Set(*Valid.Replace(TEXT("Smoke=Write"), *(FString(TEXT("Smoke=")) + Mode)));
		TestTrue(TEXT("own GUID diagnostic may use its isolated checkpoint"), Sim.Progress->IsAvailable());
		TestFalse(TEXT("contract test cannot activate fast autosaves"), Sim.Progress->IsAutosaveSmoke());
	}
	for (const FString& Invalid : { Valid.Replace(TEXT("Smoke=Write"), TEXT("Smoke=Unknown")),
		Valid.Replace(*Directory, TEXT("relative/path")), Valid.Replace(*Directory, TEXT("C:/Users/Public")),
		Valid.Replace(*Slot, TEXT("Fairhaven_Journey")), Valid.Replace(*Slot, TEXT("UEGT2_ContractSmoke_bad")),
		Valid + TEXT(" -UEGT2ProgressSmoke=Write"), Valid + TEXT(" -UEGT2AutosaveSmoke=Read"),
		Valid + TEXT(" -UEGT2ProgressSlot"), Valid + TEXT(" -UEGT2ServicesSmoke") })
	{
		FCommandLine::Set(*Invalid);
		TestFalse(TEXT("malformed/mixed diagnostics cannot reach checkpoint IO"), Sim.Progress->IsAvailable());
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
