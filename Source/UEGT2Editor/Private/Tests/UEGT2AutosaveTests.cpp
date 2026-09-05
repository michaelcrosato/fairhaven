#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Autosave/UEGT2AutosaveSubsystem.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Diagnostics/UEGT2ContractWalkSmokeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSave.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "World/UEGT2SkyController.h"
#include "../../../UEGT2/Private/Progress/UEGT2CheckpointStorage.h"

/** The runtime has one private byte boundary; fixtures replace no gameplay rules. */
struct FUEGT2AutosaveTestAccess
{
	static void SetStorage(UUEGT2ProgressSubsystem& Progress, TSharedRef<IUEGT2CheckpointStorage> Storage)
	{
		Progress.Storage = Storage;
	}
};

namespace UEGT2AutosaveTests
{
	class FHeldStorage final : public IUEGT2CheckpointStorage
	{
	public:
		struct FPending
		{
			FString Slot;
			TArray<uint8> Bytes;
			FExistsComplete Exists;
			FReadComplete Read;
			FWriteComplete Write;
		};
		TMap<FString, TArray<uint8>> Files;
		TArray<FPending> Pending;
		int32 SyncCalls = 0;
		int32 AsyncReads = 0;
		int32 ByteReads = 0;
		int32 AsyncWrites = 0;

		virtual bool Exists(const FString& Slot) override { ++SyncCalls; return Files.Contains(Slot); }
		virtual bool Read(const FString& Slot, TArray<uint8>& Bytes) override
		{
			++SyncCalls;
			const TArray<uint8>* Found = Files.Find(Slot);
			if (Found) { Bytes = *Found; }
			return Found != nullptr;
		}
		virtual bool Write(const FString& Slot, const TArray<uint8>& Bytes) override
		{
			++SyncCalls;
			Files.Add(Slot, Bytes);
			return true;
		}
		virtual void ExistsAsync(const FString& Slot, FExistsComplete Complete) override
		{
			++AsyncReads;
			FPending Op;
			Op.Slot = Slot;
			Op.Exists = MoveTemp(Complete);
			Pending.Add(MoveTemp(Op));
		}
		virtual void ReadAsync(const FString& Slot, FReadComplete Complete) override
		{
			++ByteReads;
			FPending Op;
			Op.Slot = Slot;
			Op.Read = MoveTemp(Complete);
			Pending.Add(MoveTemp(Op));
		}
		virtual void WriteAsync(const FString& Slot, const TArray<uint8>& Bytes, FWriteComplete Complete) override
		{
			++AsyncWrites;
			FPending Op;
			Op.Slot = Slot;
			Op.Bytes = Bytes;
			Op.Write = MoveTemp(Complete);
			Pending.Add(MoveTemp(Op));
		}
		bool CompleteNext(bool bSuccess = true, bool bDamageWrite = false)
		{
			if (Pending.IsEmpty()) { return false; }
			FPending Op = MoveTemp(Pending[0]);
			Pending.RemoveAt(0);
			// Completion may enqueue another operation: never retain a pointer
			// into Pending or Files across a callback.
			if (Op.Exists)
			{
				Op.Exists(!bSuccess ? EPresence::Unreadable
					: Files.Contains(Op.Slot) ? EPresence::Present : EPresence::Missing);
			}
			else if (Op.Read)
			{
				const TArray<uint8>* Found = Files.Find(Op.Slot);
				const TArray<uint8> Bytes = Found ? *Found : TArray<uint8>();
				Op.Read(bSuccess && Found != nullptr, Bytes);
			}
			else
			{
				if (bSuccess)
				{
					if (bDamageWrite) { Op.Bytes.SetNum(4); }
					Files.Add(Op.Slot, Op.Bytes);
				}
				Op.Write(bSuccess);
			}
			return true;
		}
		void Drain()
		{
			for (int32 Step = 0; Step < 12 && CompleteNext(); ++Step) {}
		}
	};

	struct FFixture
	{
		FString OriginalCommandLine = FCommandLine::Get();
		FString Slot = TEXT("UEGT2_ProgressSmoke_AutoTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bOriginalProgress = Settings && Settings->GetSaveProgressEnabled();
		bool bOriginalAuto = Settings && Settings->GetAutosaveEnabled();
		TSharedRef<FHeldStorage> Storage = MakeShared<FHeldStorage>();
		TStrongObjectPtr<UGameInstance> Instance;
		UWorld* World = nullptr;
		UUEGT2ProgressSubsystem* Progress = nullptr;
		UUEGT2AutosaveSubsystem* Timer = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		AUEGT2Character* Player = nullptr;
		AUEGT2PlayerController* Controller = nullptr;
		AUEGT2Landmark* Square = nullptr;
		APlayerState* Pauser = nullptr;
		bool bShutdown = false;

		FFixture()
		{
			if (!Settings || !GEngine) { return; }
			Settings->SetSaveProgressEnabled(true);
			Settings->SetAutosaveEnabled(true);
			FCommandLine::Set(*FString::Printf(TEXT("-UEGT2ProgressSmoke=Write -UEGT2ProgressSlot=%s"), *Slot));
			Instance.Reset(NewObject<UGameInstance>(GEngine));
			Instance->InitializeStandalone(FName(*(TEXT("AutosaveFixture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			World = Instance->GetWorld();
			if (!World) { return; }
			Progress = UUEGT2ProgressSubsystem::Get(World);
			Timer = UUEGT2AutosaveSubsystem::Get(World);
			if (!Progress || !Timer) { return; }
			FUEGT2AutosaveTestAccess::SetStorage(*Progress, Storage);
			Progress->bFeatureEnabled = true;
			Timer->bFeatureEnabled = true;
			Sky = World->SpawnActor<AUEGT2SkyController>();
			Director = UUEGT2NPCDirector::Get(World);
			Controller = World->SpawnActor<AUEGT2PlayerController>();
			Player = World->SpawnActor<AUEGT2Character>(FVector(100, 150, 120), FRotator::ZeroRotator);
			World->SpawnActor<APlayerStart>(FVector(1000, 0, 120), FRotator::ZeroRotator);
			Pauser = World->SpawnActor<APlayerState>();
			Square = World->SpawnActor<AUEGT2Landmark>(FVector(3000, 0, 0), FRotator::ZeroRotator);
			AUEGT2NPCActor* Worker = World->SpawnActor<AUEGT2NPCActor>(FVector(10000, 0, 0), FRotator::ZeroRotator);
			if (!Sky || !Director || !Controller || !Player || !Pauser || !Square || !Worker) { return; }
			// InitializeStandalone creates an uninitialized game world. Spawned
			// controllers therefore skip PostInitializeComponents, which normally
			// registers the list used by the scheduler's GetFirstPlayerController.
			World->AddController(Controller);
			Square->PersistentId = TEXT("fairhaven_square");
			Square->DispatchBeginPlay();
			Sky->SetDayLengthMinutes(4.0f);
			Director->OnWorldBeginPlay(*World);
			Director->SetCrowdDensity(1.0f);
			Director->SetSchedulesPaused(true);
			Worker->ConfigureNPC(TEXT("Autosave smith"), EUEGT2NPCRole::Smith, EUEGT2NPCSpecies::Person, 4242);
			Worker->DispatchBeginPlay();
			Director->Tick(0.0f);
			Controller->Possess(Player);
			Player->DispatchBeginPlay();
			Populate(137.625f);
			Resume();
		}
		~FFixture()
		{
			if (World) { World->DestroyWorld(false); }
			if (Instance.IsValid() && !bShutdown) { Instance->Shutdown(); }
			// Complete late callbacks after shutdown to exercise weak ownership.
			Storage->Drain();
			if (World) { GEngine->DestroyWorldContext(World); }
			if (Settings)
			{
				Settings->SetSaveProgressEnabled(bOriginalProgress);
				Settings->SetAutosaveEnabled(bOriginalAuto);
			}
			FCommandLine::Set(*OriginalCommandLine);
		}
		bool Ready() const
		{
			return World && Progress && Timer && Controller && Player && Player->GetLife()->HasBegunPlay()
				&& Sky && Director && Square && Pauser && Timer->CanAutosaveNow(Controller);
		}
		void Populate(float Coins)
		{
			FUEGT2NPCNeeds Needs;
			Needs.Energy = 0.73f; Needs.Fed = 0.42f; Needs.Relief = 0.61f; Needs.Company = 0.28f;
			Player->GetLife()->RestoreProgress(Needs, FUEGT2Purse(Coins), EUEGT2NPCRole::Smith);
			Director->RestoreCalendar(7, 2.5f, EUEGT2Weather::Cloudy);
			Controller->SetControlRotation(FRotator(-17, 123, 0));
			Square->SetDiscovered(true);
		}
		void Pause()
		{
			Controller->ShowPauseMenu();
			World->GetWorldSettings()->SetPauserPlayerState(Pauser);
		}
		void Resume()
		{
			World->GetWorldSettings()->SetPauserPlayerState(nullptr);
			Controller->CloseMenu();
			// Engine movement mode is the production grounded-eligibility input.
			// The fixture never ticks movement; packaged smoke supplies real ground.
			Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		void Main()
		{
			World->GetWorldSettings()->SetPauserPlayerState(nullptr);
			Controller->ShowMainMenu();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveRotationTest, "UEGT2.Autosave.RotationAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveRotationTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture eligible"), Sim.Ready())) { return false; }
	Sim.Pause();
	TestTrue(TEXT("manual A"), Sim.Progress->SaveProgress(Sim.Controller));
	Sim.Populate(222.125f);
	TestTrue(TEXT("manual B"), Sim.Progress->SaveProgress(Sim.Controller));
	const TArray<uint8> ManualA = Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_A"));
	const TArray<uint8> ManualB = Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_B"));
	Sim.Resume();
	Sim.Populate(311.375f);
	const int32 SyncBefore = Sim.Storage->SyncCalls;
	TestTrue(TEXT("request starts async read"), Sim.Progress->RequestAutosave(Sim.Controller));
	TestTrue(TEXT("operation held busy"), Sim.Progress->GetAutosaveStatus().bBusy);
	TestFalse(TEXT("duplicate coalesced"), Sim.Progress->RequestAutosave(Sim.Controller));
	TestEqual(TEXT("no successful write before completion"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(0));
		Sim.Storage->CompleteNext();
	Sim.Storage->CompleteNext();
	TestEqual(TEXT("one write after pair read"), Sim.Storage->AsyncWrites, 1);
	Sim.Storage->CompleteNext();
	TestEqual(TEXT("write must pass readback"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(0));
	Sim.Storage->CompleteNext();
	TestEqual(TEXT("verified commit counted"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(1));
	TestFalse(TEXT("completed no longer busy"), Sim.Progress->GetAutosaveStatus().bBusy);
	TestEqual(TEXT("active operation performs no synchronous IO"), Sim.Storage->SyncCalls, SyncBefore);
	TestTrue(TEXT("auto A exists"), Sim.Storage->Files.Contains(Sim.Slot + TEXT("_Auto_A")));

	Sim.Populate(412.875f);
	TestTrue(TEXT("second auto starts"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->Drain();
	TestTrue(TEXT("rotation writes B"), Sim.Storage->Files.Contains(Sim.Slot + TEXT("_Auto_B")));
	TestEqual(TEXT("second commit counted"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(2));
	TestTrue(TEXT("manual A bytes unchanged"), ManualA == Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_A")));
	TestTrue(TEXT("manual B bytes unchanged"), ManualB == Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_B")));

	// Damage the new write while the preceding valid automatic checkpoint stays
	// intact. Both availability and restore must select the older good envelope.
	Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_Auto_B"))[0] ^= 0x40;
	Sim.Populate(9.0f);
	Sim.Square->SetDiscovered(false);
	Sim.Main();
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	Sim.Storage->CompleteNext(); // A exists
	Sim.Storage->CompleteNext(); // A valid
	Sim.Storage->CompleteNext(); // B exists
	Sim.Storage->CompleteNext(false); // Availability can still offer A if B cannot be read.
	TestTrue(TEXT("older auto remains available"), Sim.Progress->GetAutosaveStatus().bAvailable);
	TestTrue(TEXT("explicit auto continue restores"), Sim.Progress->LoadAutosavedProgress(Sim.Controller));
	TestEqual(TEXT("fallback exact fractional purse"), Sim.Player->GetLife()->GetPurse().Coins, 311.375f);
	TestEqual(TEXT("shared restore preserves needs"), Sim.Player->GetLife()->GetNeeds().Company, 0.28f);
	TestEqual(TEXT("shared restore preserves day"), Sim.Director->GetDayIndex(), 7);
	TestEqual(TEXT("shared restore preserves hour"), Sim.Sky->GetTimeOfDay(), 2.5f);
	TestTrue(TEXT("shared restore replaces discoveries"), Sim.Square->IsDiscovered());
	TestEqual(TEXT("reads do not count writes"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(2));
	TestTrue(TEXT("manual continue still selects manual B"), Sim.Progress->LoadProgress(Sim.Controller));
	TestEqual(TEXT("manual channel independent"), Sim.Player->GetLife()->GetPurse().Coins, 222.125f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveLifecycleTest, "UEGT2.Autosave.CallbackLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture eligible"), Sim.Ready())) { return false; }
	Sim.Timer->bFeatureEnabled = false;
	TestFalse(TEXT("autosave hard off rejects IO"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Timer->bFeatureEnabled = true;
	Sim.Progress->bFeatureEnabled = false;
	TestFalse(TEXT("progress hard off rejects IO"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Progress->bFeatureEnabled = true;
	TestEqual(TEXT("hard off gates make no storage calls"), Sim.Storage->AsyncReads + Sim.Storage->ByteReads + Sim.Storage->AsyncWrites, 0);
	TestTrue(TEXT("held request"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Settings->SetAutosaveEnabled(false);
	Sim.Settings->SetAutosaveEnabled(true);
	TestFalse(TEXT("off/on cannot start competing operation"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->CompleteNext();
	TestEqual(TEXT("off/on permanently retires old read"), Sim.Storage->AsyncReads, 1);
	TestEqual(TEXT("retired read never submits write"), Sim.Storage->AsyncWrites, 0);
	TestFalse(TEXT("retired callback clears busy"), Sim.Progress->GetAutosaveStatus().bBusy);

	TestTrue(TEXT("next enabled request"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->CompleteNext();
	Sim.Storage->CompleteNext();
	TestEqual(TEXT("write was already submitted"), Sim.Storage->AsyncWrites, 1);
	Sim.Settings->SetAutosaveEnabled(false);
	const int32 ReadsBefore = Sim.Storage->ByteReads;
	Sim.Storage->CompleteNext();
	TestTrue(TEXT("uncancelable submitted bytes may finish"), Sim.Storage->Files.Contains(Sim.Slot + TEXT("_Auto_A")));
	TestEqual(TEXT("disabled completion starts no readback"), Sim.Storage->ByteReads, ReadsBefore);
	TestEqual(TEXT("disabled completion not counted"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(0));
	TestFalse(TEXT("disabled state hides available checkpoint"), Sim.Progress->GetAutosaveStatus().bAvailable);
	TestFalse(TEXT("disabled rejects new IO"), Sim.Progress->RequestAutosave(Sim.Controller));

	Sim.Settings->SetAutosaveEnabled(true);
	TestTrue(TEXT("request before new visit"), Sim.Progress->RequestAutosave(Sim.Controller));
	const uint64 Generation = Sim.Progress->GetJourneyGeneration();
	Sim.Progress->RequestNewJourney();
	Sim.Progress->SetJourneyActive(true);
	TestTrue(TEXT("new visit changes generation"), Sim.Progress->GetJourneyGeneration() > Generation);
	const int32 BeforeTravel = Sim.Storage->AsyncReads;
	Sim.Storage->CompleteNext();
	TestEqual(TEXT("previous visit read cannot enqueue more IO"), Sim.Storage->AsyncReads, BeforeTravel);
	TestEqual(TEXT("previous visit cannot write"), Sim.Storage->AsyncWrites, 1);
	TestTrue(TEXT("new visit active in actual world"), Sim.Progress->IsJourneyActive(Sim.World));
	TestFalse(TEXT("null world is never active"), Sim.Progress->IsJourneyActive(nullptr));

	TestTrue(TEXT("request before shutdown"), Sim.Progress->RequestAutosave(Sim.Controller));
	const int32 BeforeShutdown = Sim.Storage->AsyncReads;
	Sim.Instance->Shutdown();
	Sim.bShutdown = true;
	Sim.Storage->Drain();
	TestEqual(TEXT("late shutdown callback submits no more IO"), Sim.Storage->AsyncReads, BeforeShutdown);
	TestEqual(TEXT("late shutdown callback submits no write"), Sim.Storage->AsyncWrites, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveFailureTest, "UEGT2.Autosave.FailureAndCaptureBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveFailureTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture eligible"), Sim.Ready())) { return false; }
	TestTrue(TEXT("request before pause during read"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Pause();
	Sim.Storage->Drain();
	TestEqual(TEXT("pause before capture starts no write"), Sim.Storage->AsyncWrites, 0);
	Sim.Resume();
	TestTrue(TEXT("next request"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->CompleteNext(); // A missing
	Sim.Storage->CompleteNext(); // B missing, captures live gameplay and submits write
	TestEqual(TEXT("eligible snapshot submitted"), Sim.Storage->AsyncWrites, 1);
	Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Sim.Pause();
	Sim.Storage->Drain();
	TestEqual(TEXT("pause and jump after capture still verify"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(1));
	const TArray<uint8> Previous = Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_Auto_A"));
	Sim.Resume();
	TestTrue(TEXT("request with existing checkpoint"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->CompleteNext(); // A exists
	Sim.Storage->CompleteNext(false); // Existing A cannot be read
	TestFalse(TEXT("transport failure retires operation"), Sim.Progress->GetAutosaveStatus().bBusy);
	TestEqual(TEXT("unreadable existing file never starts write"), Sim.Storage->AsyncWrites, 1);
	TestTrue(TEXT("unreadable existing checkpoint unchanged"), Previous == Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_Auto_A")));
	TestTrue(TEXT("request with existence error"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->CompleteNext(false);
	TestEqual(TEXT("existence error also prevents write"), Sim.Storage->AsyncWrites, 1);

	TestTrue(TEXT("request before partial write"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->CompleteNext(); // A exists
	Sim.Storage->CompleteNext(); // A readable
	Sim.Storage->CompleteNext(); // B missing, write B
	Sim.Storage->CompleteNext(true, true); // Transport reports success but writes truncated bytes
	Sim.Storage->CompleteNext(); // Verification catches truncation
	TestEqual(TEXT("unverified bytes never count as saved"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(1));
	TestTrue(TEXT("partial rotating write preserves earlier checkpoint"), Previous == Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_Auto_A")));
	Sim.Main();
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	Sim.Storage->Drain();
	TestTrue(TEXT("partial write fallback remains usable"), Sim.Progress->LoadAutosavedProgress(Sim.Controller));
	TestEqual(TEXT("fallback restores earlier exact value"), Sim.Player->GetLife()->GetPurse().Coins, 137.625f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveCacheTest, "UEGT2.Autosave.AvailabilityCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveCacheTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture eligible"), Sim.Ready())) { return false; }
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	TestEqual(TEXT("active play does not scan availability"), Sim.Storage->AsyncReads, 0);
	Sim.Storage->Files.Add(Sim.Slot + TEXT("_Auto_A"), TArray<uint8>({ 0xff, 0xff, 0xff, 0x7f }));
	Sim.Main();
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	for (int32 Frame = 0; Frame < 120; ++Frame) { Sim.Progress->GetAutosaveStatus(); }
	TestEqual(TEXT("held menu refreshes coalesce"), Sim.Storage->AsyncReads, 1);
	TestEqual(TEXT("cached getters never synchronously read"), Sim.Storage->SyncCalls, 0);
	TestFalse(TEXT("continue while reading rejected"), Sim.Progress->LoadAutosavedProgress(Sim.Controller));
	Sim.Storage->Drain();
	TestEqual(TEXT("one completed pair despite repeated refresh"), Sim.Storage->AsyncReads, 2);
	TestFalse(TEXT("garbage envelope unavailable"), Sim.Progress->GetAutosaveStatus().bAvailable);
	TestFalse(TEXT("corruption has player-facing explanation"), Sim.Progress->GetAutosaveStatus().Text.IsEmpty());
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	TestEqual(TEXT("completed cache reused"), Sim.Storage->AsyncReads, 2);
	Sim.Settings->SetSaveProgressEnabled(false);
	Sim.Settings->SetSaveProgressEnabled(true);
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	TestEqual(TEXT("preference revision invalidates cache"), Sim.Storage->AsyncReads, 3);
	Sim.Storage->Drain();
	TestEqual(TEXT("availability never writes"), Sim.Storage->AsyncWrites, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveAvailabilityRecoveryTest, "UEGT2.Autosave.AvailabilityRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveAvailabilityRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture eligible"), Sim.Ready())) { return false; }
	TestTrue(TEXT("seed valid automatic checkpoint"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->Drain();
	const TArray<uint8> Saved = Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_Auto_A"));
	Sim.Main();
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	Sim.Storage->CompleteNext(false); // A existence check fails temporarily.
	Sim.Storage->CompleteNext(); // B is absent.
	TestFalse(TEXT("failed lookup has no available candidate"), Sim.Progress->GetAutosaveStatus().bAvailable);
	TestFalse(TEXT("failed lookup is no longer busy"), Sim.Progress->GetAutosaveStatus().bBusy);
	const int32 AfterExistsFailure = Sim.Storage->AsyncReads;
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	TestEqual(TEXT("explicit refresh retries after existence failure"), Sim.Storage->AsyncReads, AfterExistsFailure + 1);
	Sim.Storage->CompleteNext(); // A exists this time.
	Sim.Storage->CompleteNext(false); // Its bytes cannot yet be read.
	Sim.Storage->CompleteNext(); // B is still absent.
	TestFalse(TEXT("failed byte read has no available candidate"), Sim.Progress->GetAutosaveStatus().bAvailable);
	const int32 AfterReadFailure = Sim.Storage->AsyncReads;
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	TestEqual(TEXT("explicit refresh retries after byte read failure"), Sim.Storage->AsyncReads, AfterReadFailure + 1);
	Sim.Storage->Drain(); // Storage recovers, without travel or a preference change.
	TestTrue(TEXT("same menu sees recovered checkpoint"), Sim.Progress->GetAutosaveStatus().bAvailable);
	const int32 AfterRecovery = Sim.Storage->AsyncReads;
	Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
	TestEqual(TEXT("successful result remains cached"), Sim.Storage->AsyncReads, AfterRecovery);
	TestTrue(TEXT("availability never changes checkpoint bytes"), Saved == Sim.Storage->Files.FindChecked(Sim.Slot + TEXT("_Auto_A")));
	TestEqual(TEXT("availability never increments write count"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveSchedulerTest, "UEGT2.Autosave.Scheduler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveSchedulerTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture eligible"), Sim.Ready())) { return false; }
	if (!TestTrue(TEXT("scheduler resolves the registered fixture controller"),
		Sim.World->GetFirstPlayerController() == Sim.Controller)) { return false; }
	TestEqual(TEXT("normal interval is five minutes"), Sim.Timer->GetIntervalSeconds(), 300.0f);
	Sim.Timer->Tick(299.0f);
	TestEqual(TEXT("not yet due"), Sim.Storage->AsyncReads, 0);
	Sim.Pause();
	Sim.Timer->Tick(10.0f);
	TestEqual(TEXT("pause contributes no time"), Sim.Storage->AsyncReads, 0);
	Sim.Resume();
	Sim.Timer->Tick(1.0f);
	TestEqual(TEXT("resuming preserves elapsed play"), Sim.Storage->AsyncReads, 1);
	Sim.Storage->CompleteNext();
	Sim.Storage->CompleteNext();
	Sim.Storage->CompleteNext(false);
	TestEqual(TEXT("failed write not a successful save"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(0));
	const int32 AfterFailure = Sim.Storage->AsyncReads;
	Sim.Timer->Tick(4.0f);
	TestEqual(TEXT("failure retry bounded"), Sim.Storage->AsyncReads, AfterFailure);
	Sim.Timer->Tick(1.0f);
	TestEqual(TEXT("due checkpoint retries at five seconds"), Sim.Storage->AsyncReads, AfterFailure + 1);
	Sim.Storage->Drain();
	TestEqual(TEXT("retry committed"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(1));
	Sim.Timer->Tick(0.0f);
	const int32 AfterSuccess = Sim.Storage->AsyncReads;
	Sim.Timer->Tick(299.0f);
	TestEqual(TEXT("verified save resets interval"), Sim.Storage->AsyncReads, AfterSuccess);
	Sim.Pause();
	Sim.Settings->SetAutosaveEnabled(false);
	Sim.Settings->SetAutosaveEnabled(true);
	Sim.Resume();
	Sim.Timer->Tick(1.0f);
	TestEqual(TEXT("off/on while paused resets elapsed time"), Sim.Storage->AsyncReads, AfterSuccess);
	Sim.Timer->Tick(298.0f);
	TestEqual(TEXT("fresh interval still not due"), Sim.Storage->AsyncReads, AfterSuccess);
	Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Sim.Timer->Tick(1.0f);
	TestEqual(TEXT("due airborne player deferred"), Sim.Storage->AsyncReads, AfterSuccess);
	Sim.Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Sim.Timer->Tick(5.0f);
	TestEqual(TEXT("grounded retry retains due time"), Sim.Storage->AsyncReads, AfterSuccess + 1);
	Sim.Storage->Drain();
	Sim.Timer->Tick(0.0f);
	Sim.Timer->Tick(299.0f);
	Sim.Progress->RequestNewJourney();
	Sim.Progress->SetJourneyActive(true);
	const int32 BeforeNewVisit = Sim.Storage->AsyncReads;
	Sim.Timer->Tick(1.0f);
	TestEqual(TEXT("new visit resets elapsed time"), Sim.Storage->AsyncReads, BeforeNewVisit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveSmokeGateTest, "UEGT2.Autosave.IsolatedDiagnosticGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveSmokeGateTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture eligible"), Sim.Ready())) { return false; }
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Slot = TEXT("UEGT2_ProgressSmoke_") + Id;
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/AutosaveSmoke"), Id));
	const FString Valid = FString::Printf(TEXT("-UEGT2AutosaveSmoke=Write -UEGT2ProgressSlot=%s -UserDir=\"%s\""), *Slot, *Directory);
	FCommandLine::Set(*Valid);
	TestTrue(TEXT("fully isolated auto diagnostic permitted"), Sim.Progress->IsAutosaveSmoke());
	TestTrue(TEXT("isolated diagnostic keeps progress enabled"), Sim.Progress->IsEnabled());
	for (const FString& Invalid : {
		FString(TEXT("-UEGT2AutosaveSmoke")), FString(TEXT("-UEGT2AutosaveSmoke=")),
		FString(TEXT("-UEGT2AutosaveSmoke=Write")),
		Valid.Replace(TEXT("=Write"), TEXT("=Unexpected")),
		Valid.Replace(*Directory, TEXT("C:/Users/Public")),
		Valid.Replace(*Slot, TEXT("Fairhaven_Journey")),
		Valid.Replace(*Slot, TEXT("UEGT2_ProgressSmoke_not_a_guid")),
		Valid + TEXT(" -UEGT2ProgressSmoke=Write") })
	{
		FCommandLine::Set(*Invalid);
		TestFalse(TEXT("malformed diagnostic cannot reach player storage"), Sim.Progress->IsAvailable());
		TestFalse(TEXT("malformed diagnostic cannot shorten timer"), Sim.Progress->IsAutosaveSmoke());
	}
	TestEqual(TEXT("gate checks perform no IO"), Sim.Storage->SyncCalls + Sim.Storage->AsyncReads + Sim.Storage->ByteReads + Sim.Storage->AsyncWrites, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2AutosaveContractTest, "UEGT2.Autosave.ContractRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2AutosaveContractTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("autosave fixture"), Sim.Ready())) { return false; }
	UUEGT2SurveyContractSubsystem* Contract = UUEGT2SurveyContractSubsystem::Get(Sim.World);
	if (!TestNotNull(TEXT("contract service"), Contract)) { return false; }
	for (FName Id : UUEGT2SurveyContractSubsystem::RequiredLandmarkIds())
	{
		AUEGT2Landmark* Place = Sim.World->SpawnActor<AUEGT2Landmark>();
		if (!TestNotNull(TEXT("contract place"), Place)) { return false; }
		Place->PersistentId = Id;
		Place->SetDiscovered(true);
	}
	Sim.Pause();
	TestTrue(TEXT("manual checkpoint before payment"), Sim.Progress->SaveProgress(Sim.Controller));
	Sim.Resume();
	Contract->RestorePaidState(true);
	Contract->bFeatureEnabled = false;
	Sim.Populate(155.625f);
	TestTrue(TEXT("paid autosave request while contract off"), Sim.Progress->RequestAutosave(Sim.Controller));
	Sim.Storage->Drain();
	TestEqual(TEXT("verified paid autosave"), Sim.Progress->GetAutosaveStatus().SuccessfulWrites, uint64(1));
	Contract->RestorePaidState(false);
	Sim.Populate(8.25f);
	Sim.Main();
	TestTrue(TEXT("autosaved contract restores"), Sim.Progress->LoadAutosavedProgress(Sim.Controller));
	TestTrue(TEXT("auto restores paid while off"), Contract->IsPaid());
	TestEqual(TEXT("auto restores exact payment"), Sim.Player->GetLife()->GetPurse().Coins, 155.625f);
	TestTrue(TEXT("manual remains before payment"), Sim.Progress->LoadProgress(Sim.Controller));
	TestFalse(TEXT("manual restores unpaid"), Contract->IsPaid());
	TestEqual(TEXT("manual restores matching prepayment purse"), Sim.Player->GetLife()->GetPurse().Coins, 137.625f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ContractWalkPersistenceGateTest, "UEGT2.Autosave.ContractWalkDiagnosticExclusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2ContractWalkPersistenceGateTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2AutosaveTests;
	FFixture Sim;
	if (!TestTrue(TEXT("real gameplay fixture is ordinarily eligible"), Sim.Ready())) { return false; }
	const FUEGT2NPCNeeds Before = Sim.Player->GetLife()->GetNeeds();
	const float CoinsBefore = Sim.Player->GetLife()->GetPurse().Coins;
	const FVector PositionBefore = Sim.Player->GetActorLocation();

	struct FCase { const TCHAR* Name; const TCHAR* Command; };
	const FCase Cases[] = {
		{ TEXT("bare"), TEXT("-UEGT2ContractWalkSmoke") },
		{ TEXT("whole quoted bare"), TEXT("\"-UEGT2ContractWalkSmoke\"") },
		{ TEXT("lowercase"), TEXT("-uegt2contractwalksmoke") },
		{ TEXT("valued"), TEXT("-UEGT2ContractWalkSmoke=invalid") },
		{ TEXT("empty value"), TEXT("-UEGT2ContractWalkSmoke=") },
		{ TEXT("quoted empty value"), TEXT("-UEGT2ContractWalkSmoke=\"\"") },
		{ TEXT("duplicate bare"), TEXT("-UEGT2ContractWalkSmoke -UEGT2ContractWalkSmoke") },
		{ TEXT("valued and bare"), TEXT("-UEGT2ContractWalkSmoke=invalid -UEGT2ContractWalkSmoke") },
		{ TEXT("whole quoted valued"), TEXT("\"-UEGT2ContractWalkSmoke=invalid\"") }
	};
	for (const FCase& Case : Cases)
	{
		FCommandLine::Set(Case.Command);
		const FString Prefix = FString(Case.Name) + TEXT(": ");
		TestTrue(Prefix + TEXT("presence requests the diagnostic even if arguments are invalid"), UUEGT2ContractWalkSmokeSubsystem::IsRequested());
		TestFalse(Prefix + TEXT("manual persistence is unavailable"), Sim.Progress->IsAvailable());
		TestFalse(Prefix + TEXT("player preference cannot enable persistence"), Sim.Progress->IsEnabled());
		TestFalse(Prefix + TEXT("autosave is unavailable"), Sim.Timer->IsAvailable());
		TestFalse(Prefix + TEXT("autosave timer is disabled"), Sim.Timer->IsEnabled());

		// Exercise each entry point from its otherwise eligible input owner.
		// A rejected call from the wrong menu would not prove the IO gate.
		Sim.Resume();
		TestTrue(Prefix + TEXT("ordinary visit is active and unpaused"),
			Sim.Progress->IsJourneyActive(Sim.World) && !Sim.World->IsPaused()
			&& Sim.Controller->GetMenuState() == EUEGT2MenuState::None);
		TestFalse(Prefix + TEXT("automatic write is rejected"), Sim.Progress->RequestAutosave(Sim.Controller));
		Sim.Timer->Tick(Sim.Timer->GetIntervalSeconds() + 1.0f);

		Sim.Pause();
		TestTrue(Prefix + TEXT("manual save has a paused active visit"),
			Sim.World->IsPaused() && Sim.Controller->GetMenuState() == EUEGT2MenuState::Pause
			&& Sim.Progress->IsJourneyActive(Sim.World));
		TestFalse(Prefix + TEXT("paused manual save is rejected"), Sim.Progress->SaveProgress(Sim.Controller));

		Sim.Main();
		TestEqual(Prefix + TEXT("availability and Continue use Main"), Sim.Controller->GetMenuState(), EUEGT2MenuState::Main);
		TestFalse(Prefix + TEXT("manual availability performs no lookup"), Sim.Progress->HasSavedProgress());
		TestFalse(Prefix + TEXT("manual Continue is rejected"), Sim.Progress->LoadProgress(Sim.Controller));
		Sim.Progress->RefreshAutosaveAvailability(Sim.Controller);
		TestFalse(Prefix + TEXT("automatic Continue is rejected"), Sim.Progress->LoadAutosavedProgress(Sim.Controller));
		const FUEGT2AutosaveStatus Status = Sim.Progress->GetAutosaveStatus();
		TestFalse(Prefix + TEXT("no automatic availability is exposed"), Status.bAvailable);
		TestFalse(Prefix + TEXT("no asynchronous operation is busy"), Status.bBusy);
		TestEqual(Prefix + TEXT("no synchronous storage calls"), Sim.Storage->SyncCalls, 0);
		TestEqual(Prefix + TEXT("no asynchronous existence calls"), Sim.Storage->AsyncReads, 0);
		TestEqual(Prefix + TEXT("no asynchronous byte reads"), Sim.Storage->ByteReads, 0);
		TestEqual(Prefix + TEXT("no asynchronous writes"), Sim.Storage->AsyncWrites, 0);
		TestEqual(Prefix + TEXT("no held callbacks"), Sim.Storage->Pending.Num(), 0);
	}

	// Exact option names matter: an unrelated suffix must preserve normal play.
	for (const TCHAR* Control : { TEXT(""), TEXT("-UEGT2ContractWalkSmokeExtra"), TEXT("-UEGT2ContractWalkSmokeExtra=invalid") })
	{
		FCommandLine::Set(Control);
		Sim.Resume();
		TestFalse(TEXT("absent or suffixed flag is not requested"), UUEGT2ContractWalkSmokeSubsystem::IsRequested());
		TestTrue(TEXT("absent or suffixed flag preserves manual availability"), Sim.Progress->IsAvailable());
		TestTrue(TEXT("absent or suffixed flag preserves manual preference"), Sim.Progress->IsEnabled());
		TestTrue(TEXT("absent or suffixed flag preserves actual automatic eligibility"), Sim.Timer->CanAutosaveNow(Sim.Controller));
	}
	const FUEGT2NPCNeeds& After = Sim.Player->GetLife()->GetNeeds();
	TestTrue(TEXT("blocked persistence preserved all four needs"), Before.Energy == After.Energy && Before.Fed == After.Fed
		&& Before.Relief == After.Relief && Before.Company == After.Company);
	TestEqual(TEXT("blocked persistence preserved exact purse"), Sim.Player->GetLife()->GetPurse().Coins, CoinsBefore);
	TestEqual(TEXT("blocked persistence preserved trade"), Sim.Player->GetLife()->GetTrade(), EUEGT2NPCRole::Smith);
	TestTrue(TEXT("blocked persistence preserved location"), Sim.Player->GetActorLocation().Equals(PositionBefore, 0.0));
	TestTrue(TEXT("blocked persistence preserved existing discovery"), Sim.Square->IsDiscovered());
	TestTrue(TEXT("guard and control checks never touched the byte boundary"), Sim.Storage->SyncCalls == 0
		&& Sim.Storage->AsyncReads == 0 && Sim.Storage->ByteReads == 0 && Sim.Storage->AsyncWrites == 0 && Sim.Storage->Pending.IsEmpty());
	return true;
}

#endif
