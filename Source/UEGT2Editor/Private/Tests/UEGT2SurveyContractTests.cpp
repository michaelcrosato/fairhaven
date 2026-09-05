#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Contracts/UEGT2SurveyContract.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Misc/CommandLine.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Services/UEGT2ServicesSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Slate/SceneViewport.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"
#include "World/UEGT2SkyController.h"

#include <limits>

namespace UEGT2SurveyContractTests
{
	/** Real local pawn, board and Slate page, without a renderer or world ticks. */
	struct FFixture
	{
		FString OriginalCommandLine = FCommandLine::Get();
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bOriginalEnabled = Settings && Settings->GetTownSurveyContractEnabled();
		TStrongObjectPtr<UGameInstance> Instance;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		TStrongObjectPtr<UGameViewportClient> Viewport;
		TStrongObjectPtr<UGameViewportClient> OriginalViewport;
		TSharedPtr<SViewport> ViewportWidget;
		TSharedPtr<FSceneViewport> SceneViewport;
		UWorld* World = nullptr;
		AUEGT2PlayerController* Controller = nullptr;
		AUEGT2Character* Player = nullptr;
		AUEGT2SurveyContract* Board = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		APlayerState* Pauser = nullptr;
		UUEGT2SurveyContractSubsystem* Contract = nullptr;
		TArray<AUEGT2Landmark*> Landmarks;
		bool bReady = false;

		FFixture()
		{
			if (!GEngine || !Settings) { return; }
			// Menu construction must never read real checkpoints in this fixture.
			FCommandLine::Set(TEXT("-UEGT2SmokeWalk"));
			Settings->SetTownSurveyContractEnabled(true);
			Instance.Reset(NewObject<UGameInstance>(GEngine));
			Instance->InitializeStandalone(FName(*(TEXT("ContractFixture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			World = Instance->GetWorld();
			if (!World) { return; }
			Contract = UUEGT2SurveyContractSubsystem::Get(World);
			Controller = World->SpawnActor<AUEGT2PlayerController>();
			Player = World->SpawnActor<AUEGT2Character>(FVector(0, 0, 92), FRotator::ZeroRotator);
			Board = AddBoard(World);
			Pauser = World->SpawnActor<APlayerState>();
			Sky = World->SpawnActor<AUEGT2SkyController>();
			if (!Contract || !Controller || !Player || !Board || !Pauser || !Sky) { return; }
			Contract->bFeatureEnabled = true;
			Sky->TimeOfDay = 13.25f;
			World->AddController(Controller);
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));
			LocalPlayer->PlayerAdded(nullptr, 0);
			Controller->SetPlayer(LocalPlayer.Get());
			Controller->Possess(Player);
			Controller->SetControlRotation(FRotator::ZeroRotator);
			Player->DispatchBeginPlay();
			FUEGT2NPCNeeds Needs;
			Needs.Energy = 0.73f; Needs.Fed = 0.42f; Needs.Relief = 0.61f; Needs.Company = 0.28f;
			if (!Player->GetLife()->RestoreProgress(Needs, FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith)) { return; }
			for (FName Id : UUEGT2SurveyContractSubsystem::RequiredLandmarkIds())
			{
				AUEGT2Landmark* Landmark = AddLandmark(Id, false);
				if (!Landmark) { return; }
				Landmarks.Add(Landmark);
			}
			OriginalViewport.Reset(GEngine->GameViewport);
			Viewport.Reset(NewObject<UGameViewportClient>(GEngine));
			GEngine->GetWorldContextFromWorldChecked(World).GameViewport = Viewport.Get();
			ViewportWidget = SNew(SViewport);
			// UE 5.8's association creates the client's own overlay and layer
			// manager. The unattached viewport is never drawn or given a window.
			SceneViewport = Viewport->CreateViewport(ViewportWidget);
			GEngine->GameViewport = Viewport.Get();
			Controller->CloseMenu();
			bReady = Controller->IsLocalController() && Controller->GetPawn() == Player
				&& Player->GetLife()->HasBegunPlay() && Landmarks.Num() == 3
				&& Viewport->GetGameViewport() == SceneViewport.Get() && Viewport->GetGameLayerManager().IsValid();
		}

		~FFixture()
		{
			if (Controller)
			{
				if (Viewport.IsValid()) { Controller->CloseMenu(); }
				Controller->UnPossess();
			}
			if (LocalPlayer.IsValid()) { LocalPlayer->PlayerRemoved(); }
			if (SceneViewport.IsValid()) { SceneViewport->SetViewportClient(nullptr); }
			ViewportWidget.Reset();
			SceneViewport.Reset();
			if (World) { World->DestroyWorld(false); }
			if (Instance.IsValid()) { Instance->Shutdown(); }
			if (World) { GEngine->DestroyWorldContext(World); }
			if (Viewport.IsValid()) { GEngine->GameViewport = OriginalViewport.Get(); }
			if (Settings) { Settings->SetTownSurveyContractEnabled(bOriginalEnabled); }
			FCommandLine::Set(*OriginalCommandLine);
		}

		static AUEGT2SurveyContract* AddBoard(UWorld* InWorld)
		{
			AUEGT2SurveyContract* Result = InWorld ? InWorld->SpawnActor<AUEGT2SurveyContract>(FVector(180, 0, 0), FRotator::ZeroRotator) : nullptr;
			if (Result) { Result->DispatchBeginPlay(); }
			return Result;
		}
		AUEGT2Landmark* AddLandmark(FName Id, bool bDiscovered)
		{
			AUEGT2Landmark* Result = World->SpawnActor<AUEGT2Landmark>(FVector(10000, 0, 0), FRotator::ZeroRotator);
			if (Result)
			{
				Result->PersistentId = Id;
				Result->SetLandmarkName(FText::FromString(Id.ToString() + TEXT(" authored name")));
				Result->DispatchBeginPlay();
				Result->SetDiscovered(bDiscovered);
			}
			return Result;
		}
		bool Open()
		{
			// There is no GameMode in the small standalone world; use its actual
			// engine pause flag while retaining the real board/controller/page path.
			World->GetWorldSettings()->SetPauserPlayerState(Pauser);
			Board->Interact(Player);
			return World->IsPaused() && Controller->IsSurveyContractOpen()
				&& Controller->GetSurveyContractBoard() == Board;
		}
		void DiscoverAll() { for (AUEGT2Landmark* Landmark : Landmarks) { Landmark->SetDiscovered(true); } }
	};

	void CheckLife(FAutomationTestBase& Test, const FFixture& Sim, float Coins, EUEGT2Activity Activity = EUEGT2Activity::Idle)
	{
		const UUEGT2NeedsComponent* Life = Sim.Player->GetLife();
		Test.TestEqual(TEXT("energy unchanged"), Life->GetNeeds().Energy, 0.73f);
		Test.TestEqual(TEXT("food unchanged"), Life->GetNeeds().Fed, 0.42f);
		Test.TestEqual(TEXT("relief unchanged"), Life->GetNeeds().Relief, 0.61f);
		Test.TestEqual(TEXT("company unchanged"), Life->GetNeeds().Company, 0.28f);
		Test.TestEqual(TEXT("exact fractional purse"), Life->GetPurse().Coins, Coins);
		Test.TestEqual(TEXT("trade unchanged"), Life->GetTrade(), EUEGT2NPCRole::Smith);
		Test.TestEqual(TEXT("activity unchanged"), Life->GetActivity(), Activity);
		Test.TestEqual(TEXT("calendar never advanced"), Sim.Sky->GetTimeOfDay(), 13.25f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2LedgerCreditTest, "UEGT2.Economy.OneOffCredit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2LedgerCreditTest::RunTest(const FString& Parameters)
{
	FUEGT2Purse Purse(137.625f);
	TestTrue(TEXT("fractional credit succeeds"), UEGT2TryCredit(0.375f, Purse));
	TestEqual(TEXT("fractional credit exact"), Purse.Coins, 138.0f);
	TestTrue(TEXT("zero credit is harmless"), UEGT2TryCredit(0.0f, Purse));
	for (float Amount : { -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), FUEGT2Purse::MaxCoins })
	{
		TestFalse(TEXT("invalid or excessive credit rejected"), UEGT2TryCredit(Amount, Purse));
		TestEqual(TEXT("failed credit preserves exact purse"), Purse.Coins, 138.0f);
	}
	Purse.Coins = FUEGT2Purse::MaxCoins;
	TestFalse(TEXT("small reward cannot round back under the cap"), UEGT2TryCredit(18.0f, Purse));
	TestEqual(TEXT("cap failure unchanged"), Purse.Coins, FUEGT2Purse::MaxCoins);
	Purse.Coins = 134217728.0f;
	TestFalse(TEXT("unrepresentable full payment is not partially credited"), UEGT2TryCredit(18.0f, Purse));
	TestEqual(TEXT("rounding failure unchanged"), Purse.Coins, 134217728.0f);
	Purse.Coins = -2.0f;
	TestFalse(TEXT("invalid negative purse rejected"), UEGT2TryCredit(18.0f, Purse));
	TestEqual(TEXT("invalid purse never repaired by payment"), Purse.Coins, -2.0f);
	Purse.Coins = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("nonfinite purse rejected"), UEGT2TryCredit(18.0f, Purse));
	TestTrue(TEXT("nonfinite purse untouched"), FMath::IsNaN(Purse.Coins));
	for (float Start : { 7.257f, 0.1f })
	{
		Purse.Coins = Start;
		TestTrue(TEXT("ordinary rate-driven fractional purse accepts reward"), UEGT2TryCredit(18.0f, Purse));
		TestEqual(TEXT("normal float result retained"), Purse.Coins, Start + 18.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NeedsCreditTest, "UEGT2.Player.Needs.OneOffCredit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2NeedsCreditTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyContractTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real initialized life"), Sim.bReady)) { return false; }
	AUEGT2Amenity* Seat = Sim.World->SpawnActor<AUEGT2Amenity>();
	if (!TestNotNull(TEXT("real venue"), Seat)) { return false; }
	Seat->ConfigureAmenity(EUEGT2AmenityKind::Seat, TEXT("Credit test seat"), EUEGT2NPCRole::Villager);
	Seat->DispatchBeginPlay();
	UUEGT2NeedsComponent* Life = Sim.Player->GetLife();
	TestTrue(TEXT("activity actually starts"), Life->BeginActivity(EUEGT2Activity::Rest, Seat,
		Seat->GetVenueName(), EUEGT2NPCRole::Villager, 400.0f));
	int32 Notifications = 0;
	const FDelegateHandle Handle = Life->OnActivityChanged.AddLambda([&](EUEGT2Activity, const FText&) { ++Notifications; });
	TestTrue(TEXT("component applies shared one-off payment"), Life->TryCredit(18.0f));
	CheckLife(*this, Sim, 155.625f, EUEGT2Activity::Rest);
	TestTrue(TEXT("credit preserves occupied venue"), Life->IsUsing(Seat));
	TestEqual(TEXT("payment emits no activity delegate"), Notifications, 0);
	TestFalse(TEXT("invalid credit refused"), Life->TryCredit(std::numeric_limits<float>::infinity()));
	CheckLife(*this, Sim, 155.625f, EUEGT2Activity::Rest);
	Life->OnActivityChanged.Remove(Handle);
	TStrongObjectPtr<UUEGT2NeedsComponent> Unbegun(NewObject<UUEGT2NeedsComponent>(Sim.Player));
	TestFalse(TEXT("unbegun component cannot accept durable payment"), Unbegun->TryCredit(18.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ContractEntriesTest, "UEGT2.Contracts.EntriesAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2ContractEntriesTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyContractTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real contract fixture"), Sim.bReady)) { return false; }
	const TConstArrayView<FName> Ids = UUEGT2SurveyContractSubsystem::RequiredLandmarkIds();
	TestEqual(TEXT("one fixed three-place contract"), Ids.Num(), 3);
	TestEqual(TEXT("reward derives from two courier errand hours"), Sim.Contract->GetReward(),
		2.0f * UEGT2WageFor(EUEGT2NPCRole::Courier, EUEGT2Activity::Errand));
	TArray<FUEGT2SurveyContractEntry> Entries = Sim.Contract->GetEntries(Sim.Controller);
	if (!TestEqual(TEXT("three explicit rows"), Entries.Num(), 3)) { return false; }
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		TestEqual(TEXT("fixed row order"), Entries[Index].Id, Ids[Index]);
		TestTrue(TEXT("unique authored place available"), Entries[Index].bAvailable);
		TestTrue(TEXT("authored name retained"), Entries[Index].Name.EqualTo(Sim.Landmarks[Index]->GetLandmarkName()));
		TestFalse(TEXT("reading never discovers place"), Entries[Index].bDiscovered);
		TestTrue(TEXT("direction calculated"), Entries[Index].bHasDirection);
		TestEqual(TEXT("horizontal distance in metres"), Entries[Index].Direction.DistanceMetres, 100.0f);
	}
	Sim.Landmarks[0]->SetDiscovered(true);
	Sim.Landmarks[1]->SetDiscovered(true);
	Entries = Sim.Contract->GetEntries(Sim.Controller);
	TestTrue(TEXT("prior surveys count immediately"), Entries[0].bDiscovered && Entries[1].bDiscovered);
	TestFalse(TEXT("third remains unsurveyed"), Entries[2].bDiscovered);
	AUEGT2Landmark* Duplicate = Sim.AddLandmark(Ids[0], true);
	if (!TestNotNull(TEXT("duplicate required identity"), Duplicate)) { return false; }
	Entries = Sim.Contract->GetEntries(Sim.Controller);
	TestFalse(TEXT("duplicate identity is unavailable even when both surveyed"), Entries[0].bAvailable);
	TestFalse(TEXT("ambiguous row cannot imply progress"), Entries[0].bDiscovered);
	TestTrue(TEXT("duplicate destroyed"), Duplicate->Destroy());
	TestTrue(TEXT("remaining unique identity resolves"), Sim.Contract->GetEntries(Sim.Controller)[0].bAvailable);
	Sim.Landmarks[2]->PersistentId = TEXT("renamed_unknown_place");
	Entries = Sim.Contract->GetEntries(Sim.Controller);
	TestFalse(TEXT("missing required ID cannot fall back to another actor"), Entries[2].bAvailable);
	TestFalse(TEXT("missing row has no direction"), Entries[2].bHasDirection);
	TestEqual(TEXT("invalid observer gives no snapshot"), Sim.Contract->GetEntries(nullptr).Num(), 0);
	CheckLife(*this, Sim, 137.625f);
	TestFalse(TEXT("enumeration never pays"), Sim.Contract->IsPaid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ContractTransactionTest, "UEGT2.Contracts.Transaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2ContractTransactionTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyContractTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real local contract fixture"), Sim.bReady)) { return false; }
	FText Reason;
	TestFalse(TEXT("live call cannot pay"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
	if (!TestTrue(TEXT("real board opens its paused contract page"), Sim.Open())) { return false; }
	TestFalse(TEXT("zero discoveries cannot claim"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
	Sim.Landmarks[0]->SetDiscovered(true); Sim.Landmarks[1]->SetDiscovered(true);
	TestFalse(TEXT("two discoveries cannot claim"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
	Sim.DiscoverAll();
	TestTrue(TEXT("all three surveyed become eligible"), Sim.Contract->CanClaim(Sim.Controller, Sim.Board, Reason));
	TestTrue(TEXT("valid preview clears reason"), Reason.IsEmpty());
	CheckLife(*this, Sim, 137.625f);
	TestFalse(TEXT("readiness does not pay"), Sim.Contract->IsPaid());
	int32 Notifications = 0;
	const FDelegateHandle Handle = Sim.Player->GetLife()->OnActivityChanged.AddLambda([&](EUEGT2Activity, const FText&) { ++Notifications; });
	TestTrue(TEXT("production transaction succeeds"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
	TestTrue(TEXT("paid flag committed"), Sim.Contract->IsPaid());
	CheckLife(*this, Sim, 155.625f);
	TestEqual(TEXT("no delegate between credit and paid state"), Notifications, 0);
	TestTrue(TEXT("successful claim stays on the page"), Sim.Controller->IsSurveyContractOpen());
	TestFalse(TEXT("duplicate callback cannot pay twice"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
	TestFalse(TEXT("paid state refuses further eligibility"), Sim.Contract->CanClaim(Sim.Controller, Sim.Board, Reason));
	CheckLife(*this, Sim, 155.625f);
	Sim.Player->GetLife()->OnActivityChanged.Remove(Handle);
	// A loaded earlier checkpoint may replace both paid state and purse. Real
	// life produces nonbinary fractions, which must still accept this reward.
	for (float Start : { 7.257f, 0.1f })
	{
		Sim.Contract->RestorePaidState(false);
		Sim.Player->GetLife()->SetCoins(Start);
		TestTrue(TEXT("real fractional purse can collect full reward"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
		CheckLife(*this, Sim, Start + Sim.Contract->GetReward());
		TestTrue(TEXT("fractional payment commits paid state"), Sim.Contract->IsPaid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ContractGuardsTest, "UEGT2.Contracts.GuardsAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2ContractGuardsTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyContractTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real contract fixture"), Sim.bReady)) { return false; }
	Sim.DiscoverAll();
	if (!TestTrue(TEXT("own board page opens"), Sim.Open())) { return false; }
	FText Reason;
	auto Reject = [&](const TCHAR* Label, AUEGT2SurveyContract* Board)
	{
		TestFalse(Label, Sim.Contract->TryClaim(Sim.Controller, Board, Reason));
		TestFalse(FString(Label) + TEXT(" supplies a reason"), Reason.IsEmpty());
		TestFalse(FString(Label) + TEXT(" preserves entitlement"), Sim.Contract->IsPaid());
		CheckLife(*this, Sim, 137.625f);
	};
	Reject(TEXT("null board"), nullptr);
	AUEGT2SurveyContract* OtherBoard = FFixture::AddBoard(Sim.World);
	if (!TestNotNull(TEXT("second board"), OtherBoard)) { return false; }
	Reject(TEXT("page cannot authorize another board"), OtherBoard);
	OtherBoard->Destroy();
	Reject(TEXT("destroyed board"), OtherBoard);
	UWorld* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("second world"), OtherWorld)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(OtherWorld);
	AUEGT2SurveyContract* ForeignBoard = FFixture::AddBoard(OtherWorld);
	Reject(TEXT("foreign-world board"), ForeignBoard);
	UUEGT2SurveyContractSubsystem* OtherContract = UUEGT2SurveyContractSubsystem::Get(OtherWorld);
	TestNotNull(TEXT("second world has independent contract"), OtherContract);
	if (OtherContract) { TestFalse(TEXT("new world starts unpaid"), OtherContract->IsPaid()); }
	OtherWorld->DestroyWorld(false); GEngine->DestroyWorldContext(OtherWorld);
	Sim.Board->SetUseRange(10.0f); Reject(TEXT("too distant"), Sim.Board);
	Sim.Board->SetUseRange(std::numeric_limits<float>::infinity()); Reject(TEXT("invalid use range"), Sim.Board);
	Sim.Board->SetUseRange(340.0f);
	AUEGT2Landmark* Duplicate = Sim.AddLandmark(Sim.Landmarks[0]->GetPersistentId(), true);
	if (!TestNotNull(TEXT("duplicate actor for commit validation"), Duplicate)) { return false; }
	Reject(TEXT("ambiguous landmark at commit"), Sim.Board);
	if (Duplicate) { Duplicate->Destroy(); }
	Sim.Landmarks[1]->SetDiscovered(false); Reject(TEXT("discovery reset after opening"), Sim.Board);
	Sim.Landmarks[1]->SetDiscovered(true);
	for (float Coins : { FUEGT2Purse::MaxCoins, 134217728.0f, std::numeric_limits<float>::infinity() })
	{
		Sim.Player->GetLife()->SetCoins(Coins);
		TestFalse(TEXT("invalid/capped purse prevents transaction"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
		TestTrue(TEXT("failed transaction preserves purse including infinity"), Sim.Player->GetLife()->GetPurse().Coins == Coins);
		TestFalse(TEXT("failed credit preserves entitlement"), Sim.Contract->IsPaid());
	}
	Sim.Player->GetLife()->SetCoins(137.625f);
	Sim.Settings->SetTownSurveyContractEnabled(false);
	Reject(TEXT("player gate off"), Sim.Board);
	Sim.Settings->SetTownSurveyContractEnabled(true);
	Sim.Contract->bFeatureEnabled = false;
	Reject(TEXT("maintainer gate off"), Sim.Board);
	TestEqual(TEXT("disabled guide has no entries"), Sim.Contract->GetEntries(Sim.Controller).Num(), 0);
	Sim.Contract->RestorePaidState(true);
	TestTrue(TEXT("paid restore works while disabled"), Sim.Contract->IsPaid());
	Sim.Contract->RestorePaidState(true);
	CheckLife(*this, Sim, 137.625f);
	Sim.Contract->bFeatureEnabled = true;
	TestFalse(TEXT("reenabling cannot repay restored completion"), Sim.Contract->TryClaim(Sim.Controller, Sim.Board, Reason));
	Sim.Contract->RestorePaidState(false);
	TestTrue(TEXT("unpaid restore replaces durable state without payment"), Sim.Contract->CanClaim(Sim.Controller, Sim.Board, Reason));
	CheckLife(*this, Sim, 137.625f);
	Sim.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Reject(TEXT("page without paused world"), Sim.Board);
	Sim.World->GetWorldSettings()->SetPauserPlayerState(Sim.Pauser);
	Sim.Controller->CloseMenu();
	Reject(TEXT("paused world without contract page"), Sim.Board);
	Sim.Contract->bFeatureEnabled = false;
	TestTrue(TEXT("disabled sign remains readable"), Sim.Board->CanInteract(Sim.Player));
	Sim.Board->Interact(Sim.Player);
	TestFalse(TEXT("disabled sign never opens contract page"), Sim.Controller->IsSurveyContractOpen());
	CheckLife(*this, Sim, 137.625f);
	return true;
}

#endif
