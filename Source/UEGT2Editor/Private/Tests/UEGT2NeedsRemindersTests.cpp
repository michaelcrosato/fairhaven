#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "NPC/UEGT2NPCActor.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UI/UEGT2HUD.h"
#include "UI/UEGT2NeedReminders.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

namespace UEGT2NeedsRemindersTests
{
	FUEGT2NPCNeeds Values(float Energy, float Fed, float Relief, float Company)
	{
		FUEGT2NPCNeeds Result;
		Result.Energy = Energy; Result.Fed = Fed; Result.Relief = Relief; Result.Company = Company;
		return Result;
	}

	/** Actual HUD-owned FTimerManager callback and player component. No actor ticks,
	 * map, settings IO or renderer: the fixture advances only the engine clock and
	 * timer phase, following LevelTick's pause condition. Packaged tests cover draw. */
	struct FFixture
	{
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bOriginalEnabled = Settings && Settings->GetNeedsRemindersEnabled();
		bool bOriginalPanel = Settings && Settings->GetShowNeeds();
		bool bOriginalServices = Settings && Settings->GetNearbyServicesEnabled();
		uint64 OriginalFrame = GFrameCounter;
		UWorld* World = nullptr;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		AUEGT2PlayerController* Controller = nullptr;
		AUEGT2Character* Player = nullptr;
		AUEGT2HUD* HUD = nullptr;
		APlayerState* Pauser = nullptr;
		bool bReady = false;

		FFixture()
		{
			if (!Settings || !GEngine) { return; }
			Settings->SetNeedsRemindersEnabled(true);
			Settings->SetNearbyServicesEnabled(true);
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World) { return; }
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Controller = World->SpawnActor<AUEGT2PlayerController>();
			Player = World->SpawnActor<AUEGT2Character>();
			Pauser = World->SpawnActor<APlayerState>();
			if (!Controller || !Player || !Pauser) { return; }
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));
			LocalPlayer->PlayerAdded(nullptr, 0);
			Controller->SetPlayer(LocalPlayer.Get());
			Controller->Possess(Player);
			Player->DispatchBeginPlay();
			FActorSpawnParameters Parameters;
			Parameters.Owner = Controller;
			HUD = World->SpawnActor<AUEGT2HUD>(Parameters);
			if (!HUD) { return; }
			// Standalone fixture worlds have not initialized all actors. This is
			// the engine's normal HUD association from PostInitializeComponents.
			HUD->PlayerOwner = Controller;
			HUD->bNeedsRemindersEnabled = true;
			HUD->DispatchBeginPlay();
			bReady = Controller->IsLocalController() && HUD->HasActorBegunPlay() && Life()->HasBegunPlay();
		}

		~FFixture()
		{
			if (Controller) { Controller->CloseDialogue(); Controller->UnPossess(); }
			if (LocalPlayer.IsValid()) { LocalPlayer->PlayerRemoved(); }
			if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
			if (Settings)
			{
				Settings->SetNeedsRemindersEnabled(bOriginalEnabled);
				Settings->SetShowNeeds(bOriginalPanel);
				Settings->SetNearbyServicesEnabled(bOriginalServices);
			}
			GFrameCounter = OriginalFrame;
		}

		UUEGT2NeedsComponent* Life() const { return Player->GetLife(); }
		bool Seed(const FUEGT2NPCNeeds& Needs)
		{
			const bool bRestored = Life()->RestoreProgress(Needs, FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith);
			HUD->GetNeedsReminderText(); // Same revision check as the first draw, without a Canvas.
			return bRestored;
		}
		void Advance(double Seconds)
		{
			while (Seconds > 0.00001)
			{
				const float Step = static_cast<float>(FMath::Min(Seconds, 0.1));
				++GFrameCounter;
				if (!World->IsPaused())
				{
					World->TimeSeconds += Step;
					World->GetTimerManager().Tick(Step);
				}
				Seconds -= Step;
			}
		}
		bool AwaitReminder(double Maximum = 6.0)
		{
			for (double Waited = 0; Waited < Maximum; Waited += 0.1)
			{
				Advance(0.1);
				if (HUD->GetNeedsReminderMask()) { return true; }
			}
			return false;
		}
		void Pause()
		{
			Controller->ShowPauseMenu();
			World->GetWorldSettings()->SetPauserPlayerState(Pauser);
			HUD->GetNeedsReminderText(); // The display query checks lifecycle even while timers are paused.
		}
		void Resume()
		{
			World->GetWorldSettings()->SetPauserPlayerState(nullptr);
			Controller->CloseMenu();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NeedsRemindersPolicyTest, "UEGT2.Player.NeedsReminders.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2NeedsRemindersPolicyTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NeedReminders;
	using UEGT2NeedsRemindersTests::Values;
	FState State;
	State.Reset(100);
	TestTrue(TEXT("finite boundary snapshot"), State.Observe(Values(1, .34f, 1, 1)));
	TestEqual(TEXT("exact amber boundary is not low"), State.Ready(106), uint8(0));
	State.Observe(Values(1, .339f, .1f, 1));
	TestEqual(TEXT("initial grace suppresses coalesced lows"), State.Ready(104.999), uint8(0));
	TestEqual(TEXT("all pending needs share one notice"), State.Ready(105), uint8(Fed | Relief));
	State.Delivered(State.Ready(105), 105);
	State.Observe(Values(1, 0, 0, 1));
	TestEqual(TEXT("remaining empty never nags"), State.Ready(1000), uint8(0));
	State.Observe(Values(1, .499f, 0, 1));
	State.Observe(Values(1, .1f, 0, 1));
	TestEqual(TEXT("partial recovery does not re-arm"), State.Ready(1000), uint8(0));
	State.Observe(Values(1, .50f, 0, 1));
	State.Observe(Values(1, .1f, 0, 1));
	TestEqual(TEXT("global cooldown survives recovery"), State.Ready(134.999), uint8(0));
	TestEqual(TEXT("exact recovery boundary re-arms after cooldown"), State.Ready(135), uint8(Fed));
	State.Observe(Values(1, .34f, 0, 1));
	TestEqual(TEXT("recovery clears an undelivered low"), State.Ready(135), uint8(0));
	State.Observe(Values(1, .1f, 0, 1));
	TestEqual(TEXT("canceled pending episode also needs substantial recovery"), State.Ready(136), uint8(0));
	State.Observe(Values(.1f, .34f, 0, 1));
	TestFalse(TEXT("invalid snapshot is rejected as a whole"), State.Observe(Values(.9f, .9f, .9f, std::numeric_limits<float>::quiet_NaN())));
	TestEqual(TEXT("invalid input cannot re-arm or discard another need"), State.Ready(135), uint8(Energy));
	TestEqual(TEXT("nonfinite clock cannot deliver"), State.Ready(std::numeric_limits<double>::infinity()), uint8(0));
	State.Reset(200);
	State.Observe(Values(0, 0, 0, 0));
	TestEqual(TEXT("reset drops history and starts fresh grace"), State.Ready(204.999), uint8(0));
	TestEqual(TEXT("already-low restored snapshot warns once after grace"), State.Ready(205), uint8(All));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NeedsRemindersPriorityTest, "UEGT2.Player.NeedsReminders.Priority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2NeedsRemindersPriorityTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NeedsRemindersTests;
	using namespace UEGT2NeedReminders;
	FFixture Sim;
	if (!TestTrue(TEXT("real local HUD and timer fixture"), Sim.bReady)) { return false; }
	TestTrue(TEXT("seed above food threshold"), Sim.Seed(Values(.9f, .345f, .9f, .9f)));
	Sim.Advance(5.6);
	TestTrue(TEXT("healthy timer samples are quiet"), Sim.HUD->GetNeedsReminderText().IsEmpty());
	const uint64 Revision = Sim.Life()->GetNeedsRevision();
	Sim.Life()->AdvanceLife(.1f);
	TestEqual(TEXT("ordinary elapsed life is not a restore"), Sim.Life()->GetNeedsRevision(), Revision);
	const FUEGT2NPCNeeds Before = Sim.Life()->GetNeeds();
	const float CoinsBefore = Sim.Life()->GetPurse().Coins;
	const FText Notice = FText::FromString(TEXT("You leave the counter."));
	Sim.HUD->ShowMessage(Notice, 2.0f);
	Sim.Advance(1.5);
	TestEqual(TEXT("action feedback retains priority"), Sim.HUD->GetOrdinaryMessageText().ToString(), Notice.ToString());
	TestEqual(TEXT("pending low cannot replace action feedback"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	TestTrue(TEXT("real timer delivers after ordinary expiry"), Sim.AwaitReminder());
	TestEqual(TEXT("actual shared-ledger food crossing"), Sim.HUD->GetNeedsReminderMask(), uint8(Fed));
	TestTrue(TEXT("available guide is actionable"), Sim.HUD->GetNeedsReminderText().ToString().Contains(TEXT("Nearby Services")));
	Sim.HUD->ShowMessage(Notice, 1.0f);
	TestEqual(TEXT("new ordinary notice immediately retires reminder"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Advance(40);
	TestTrue(TEXT("interrupted warning does not replay"), Sim.HUD->GetNeedsReminderText().IsEmpty());
	TestEqual(TEXT("timer leaves exact food value"), Sim.Life()->GetNeeds().Fed, Before.Fed);
	TestEqual(TEXT("timer leaves exact energy value"), Sim.Life()->GetNeeds().Energy, Before.Energy);
	TestEqual(TEXT("timer leaves exact relief value"), Sim.Life()->GetNeeds().Relief, Before.Relief);
	TestEqual(TEXT("timer leaves exact company value"), Sim.Life()->GetNeeds().Company, Before.Company);
	TestEqual(TEXT("timer leaves fractional purse"), Sim.Life()->GetPurse().Coins, CoinsBefore);
	TestEqual(TEXT("timer leaves trade"), Sim.Life()->GetTrade(), EUEGT2NPCRole::Smith);
	TestEqual(TEXT("timer leaves activity"), Sim.Life()->GetActivity(), EUEGT2Activity::Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NeedsRemindersLifecycleTest, "UEGT2.Player.NeedsReminders.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2NeedsRemindersLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NeedsRemindersTests;
	using namespace UEGT2NeedReminders;
	FFixture Sim;
	if (!TestTrue(TEXT("real local HUD and timer fixture"), Sim.bReady)) { return false; }
	TestTrue(TEXT("restore low player"), Sim.Seed(Values(.1f, .1f, .1f, .1f)));
	Sim.Settings->SetShowNeeds(false);
	Sim.Advance(4.5);
	TestEqual(TEXT("initial grace"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	AUEGT2NPCActor* Partner = Sim.World->SpawnActor<AUEGT2NPCActor>();
	if (!TestNotNull(TEXT("real dialogue partner"), Partner)) { return false; }
	Sim.Controller->OpenDialogue(Partner);
	Sim.Advance(3);
	TestTrue(TEXT("actual dialogue is open"), Sim.Controller->IsDialogueOpen());
	TestEqual(TEXT("live dialogue defers low needs"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Controller->CloseDialogue();
	TestTrue(TEXT("coalesced reminder works with needs panel hidden"), Sim.AwaitReminder());
	TestEqual(TEXT("all four needs coalesce"), Sim.HUD->GetNeedsReminderMask(), uint8(All));
	Sim.Pause();
	TestTrue(TEXT("actual world is paused"), Sim.World->IsPaused());
	TestEqual(TEXT("paused display query retires visible reminder"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	const double PausedAt = Sim.World->GetTimeSeconds();
	Sim.Advance(60);
	TestEqual(TEXT("pause does not charge timer clock"), Sim.World->GetTimeSeconds(), PausedAt);
	Sim.Resume();
	Sim.Advance(35);
	TestEqual(TEXT("pause does not re-arm a displayed warning"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	const uint64 BeforeRevision = Sim.Life()->GetNeedsRevision();
	TestFalse(TEXT("invalid restore rejected"), Sim.Life()->RestoreProgress(Values(-1, .1f, .1f, .1f), FUEGT2Purse(1), EUEGT2NPCRole::Smith));
	TestEqual(TEXT("invalid restore keeps revision"), Sim.Life()->GetNeedsRevision(), BeforeRevision);
	TestTrue(TEXT("successful low restore"), Sim.Life()->RestoreProgress(Values(.9f, .9f, .9f, .1f), FUEGT2Purse(99.125f), EUEGT2NPCRole::Courier));
	TestEqual(TEXT("successful restore increments revision"), Sim.Life()->GetNeedsRevision(), BeforeRevision + 1);
	Sim.HUD->GetNeedsReminderText();
	Sim.Advance(4.5);
	TestEqual(TEXT("restore starts grace without reusing old mask"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	TestTrue(TEXT("fresh restored low arrives after grace"), Sim.AwaitReminder());
	TestEqual(TEXT("restored identity is company only"), Sim.HUD->GetNeedsReminderMask(), uint8(Company));
	TestFalse(TEXT("company alone is not falsely routed through services"), Sim.HUD->GetNeedsReminderText().ToString().Contains(TEXT("Nearby Services")));
	Sim.Life()->SetNeedsSatisfied(true);
	TestEqual(TEXT("dev replacement also changes revision"), Sim.Life()->GetNeedsRevision(), BeforeRevision + 2);
	TestEqual(TEXT("getter retires pre-reset text before timer"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Advance(6);
	TestEqual(TEXT("full reset stays quiet"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2NeedsRemindersDisabledTest, "UEGT2.Player.NeedsReminders.DisabledAndOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2NeedsRemindersDisabledTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2NeedsRemindersTests;
	using namespace UEGT2NeedReminders;
	FFixture Sim;
	if (!TestTrue(TEXT("real local HUD and timer fixture"), Sim.bReady)) { return false; }
	TestTrue(TEXT("seed low player"), Sim.Seed(Values(.1f, .1f, .1f, .1f)));
	TestTrue(TEXT("initial reminder"), Sim.AwaitReminder());
	Sim.Settings->SetNeedsRemindersEnabled(false);
	UUEGT2GameUserSettings::OnSettingsApplied.Broadcast();
	TestEqual(TEXT("player off clears visible reminder immediately"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	const FText Notice = FText::FromString(TEXT("An ordinary notice survives feature off."));
	Sim.HUD->ShowMessage(Notice, 100);
	Sim.Advance(35);
	TestEqual(TEXT("off timer cannot change ordinary notice"), Sim.HUD->GetOrdinaryMessageText().ToString(), Notice.ToString());
	TestEqual(TEXT("off remains quiet"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Settings->SetNeedsRemindersEnabled(true);
	UUEGT2GameUserSettings::OnSettingsApplied.Broadcast();
	Sim.HUD->ShowMessage(FText::GetEmpty());
	Sim.Advance(4.5);
	TestEqual(TEXT("re-enable has new grace"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	TestTrue(TEXT("re-enable can report current low state"), Sim.AwaitReminder());
	Sim.HUD->bNeedsRemindersEnabled = false;
	Sim.HUD->GetNeedsReminderText();
	TestEqual(TEXT("maintainer off clears before the next timer"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Advance(35);
	TestEqual(TEXT("maintainer off cannot deliver"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Settings->SetNearbyServicesEnabled(false);
	Sim.HUD->bNeedsRemindersEnabled = true;
	Sim.HUD->GetNeedsReminderText();
	TestTrue(TEXT("independent guide off still allows reminders"), Sim.AwaitReminder());
	TestFalse(TEXT("disabled guide is never advertised"), Sim.HUD->GetNeedsReminderText().ToString().Contains(TEXT("Nearby Services")));
	TestTrue(TEXT("guide off retains direct action hint"), Sim.HUD->GetNeedsReminderText().ToString().Contains(TEXT("talk to someone")));
	Sim.Controller->ShowMainMenu();
	Sim.HUD->GetNeedsReminderText();
	Sim.Advance(8);
	TestEqual(TEXT("Main cannot show or accumulate reminders"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Controller->CloseMenu();
	Sim.HUD->GetNeedsReminderText();
	TestTrue(TEXT("active visit starts a fresh episode"), Sim.AwaitReminder());
	AUEGT2Character* Replacement = Sim.World->SpawnActor<AUEGT2Character>();
	if (!TestNotNull(TEXT("replacement pawn"), Replacement)) { return false; }
	Replacement->DispatchBeginPlay();
	Sim.Controller->Possess(Replacement);
	TestEqual(TEXT("pawn replacement retires old text immediately"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.Advance(6);
	TestEqual(TEXT("healthy replacement inherits no warning history"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	Sim.HUD->Destroy();
	Sim.Advance(60);
	UUEGT2GameUserSettings::OnSettingsApplied.Broadcast();
	TestEqual(TEXT("ended HUD never delivers"), Sim.HUD->GetNeedsReminderMask(), uint8(0));
	return true;
}

#endif
