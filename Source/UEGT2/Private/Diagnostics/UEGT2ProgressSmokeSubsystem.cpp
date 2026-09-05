#include "Diagnostics/UEGT2ProgressSmokeSubsystem.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2ProgressSmoke
{
	constexpr float SavedHour = 13.25f;
	constexpr int32 SavedDay = 7;
	const FRotator SavedView(-9.0, 71.5, 0.0);
	const FName LandmarkId(TEXT("fairhaven_square"));

	FUEGT2NPCNeeds SavedNeeds()
	{
		FUEGT2NPCNeeds Needs;
		Needs.Energy = 0.73f;
		Needs.Fed = 0.42f;
		Needs.Relief = 0.61f;
		Needs.Company = 0.28f;
		return Needs;
	}

	bool NeedsEqual(const FUEGT2NPCNeeds& A, const FUEGT2NPCNeeds& B, float Tolerance)
	{
		return FMath::IsNearlyEqual(A.Energy, B.Energy, Tolerance)
			&& FMath::IsNearlyEqual(A.Fed, B.Fed, Tolerance)
			&& FMath::IsNearlyEqual(A.Relief, B.Relief, Tolerance)
			&& FMath::IsNearlyEqual(A.Company, B.Company, Tolerance);
	}

	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		AUEGT2Character* Player = nullptr;
		UUEGT2NeedsComponent* Life = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		UUEGT2ProgressSubsystem* Progress = nullptr;
		AUEGT2Landmark* Landmark = nullptr;
		APlayerStart* Start = nullptr;

		explicit FContext(UWorld* World)
		{
			if (!World) { return; }
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
			Life = Player ? Player->GetLife() : nullptr;
			Director = UUEGT2NPCDirector::Get(World);
			Sky = AUEGT2SkyController::Get(World);
			Progress = UUEGT2ProgressSubsystem::Get(World);
			for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
			{
				if (It->GetPersistentId() == LandmarkId) { Landmark = *It; }
			}
			for (TActorIterator<APlayerStart> It(World); It; ++It)
			{
				if (!Start || It->GetName() < Start->GetName()) { Start = *It; }
			}
		}

		bool IsValid() const
		{
			return PC && Player && Life && Life->HasBegunPlay() && Director && Sky && Progress && Landmark && Start;
		}

		FVector SavedLocation() const
		{
			// Distinct from a fresh spawn, clear of the ground, and deterministic
			// across processes. Pause before saving so gravity cannot alter it.
			return Start->GetActorLocation() + FVector(180.0, 100.0, 150.0);
		}
	};

	struct FSlotBytes
	{
		bool bExists[2] = { false, false };
		TArray<uint8> Data[2];

		void Read(const FString& Slot)
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Data[Index].Reset();
				bExists[Index] = UGameplayStatics::LoadDataFromSlot(Data[Index], Slot + (Index == 0 ? TEXT("_A") : TEXT("_B")), 0);
			}
		}

		bool HasCheckpoint() const { return bExists[0] || bExists[1]; }
		bool Equals(const FSlotBytes& Other) const
		{
			return bExists[0] == Other.bExists[0] && bExists[1] == Other.bExists[1]
				&& Data[0] == Other.Data[0] && Data[1] == Other.Data[1];
		}
	};

	// Only this opt-in diagnostic carries state across the New Visit map reload.
	bool bNewVisitRequested = false;
	FSlotBytes BeforeNewVisit;

	void ClearLandmarks(UWorld* World)
	{
		for (TActorIterator<AUEGT2Landmark> It(World); It; ++It) { It->SetDiscovered(false); }
	}
}

bool UUEGT2ProgressSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString RequestedPhase;
	return FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), RequestedPhase)
		&& Super::ShouldCreateSubsystem(Outer);
}

bool UUEGT2ProgressSmokeSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game;
}

TStatId UUEGT2ProgressSmokeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2ProgressSmokeSubsystem, STATGROUP_Tickables);
}

void UUEGT2ProgressSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRequested = FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), Phase);
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSlot="), Slot);
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressCapture="), CaptureDirectory);
	StartedSeconds = FPlatformTime::Seconds();
	bNewVisitWorld = UEGT2ProgressSmoke::bNewVisitRequested;
	FString Suffix = Slot;
	FGuid RunId;
	const bool bOwnSlot = Suffix.RemoveFromStart(TEXT("UEGT2_ProgressSmoke_"), ESearchCase::CaseSensitive)
		&& FGuid::ParseExact(Suffix, EGuidFormats::Digits, RunId);
	if (!Check(bOwnSlot, TEXT("expected a unique UEGT2_ProgressSmoke_<guid> slot"))) { return; }
	if (!Check(Phase == TEXT("Write") || Phase == TEXT("Read") || Phase == TEXT("NewVisit") || Phase == TEXT("Disabled"),
		TEXT("unknown progress smoke phase"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
		&& !UUEGT2CaptureSubsystem::IsFlySoakRequested(), TEXT("progress smoke cannot share another diagnostic run"))) { return; }
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(Phase == TEXT("Read") && !FPaths::IsRelative(CaptureDirectory), TEXT("capture needs Read phase and an absolute directory"))) { return; }
		if (!Check(IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("cannot create progress capture directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2ProgressSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Progress smoke starting: phase=%s slot=%s"), *Phase, *Slot);
}

void UUEGT2ProgressSmokeSubsystem::RestorePreference()
{
	if (bPreferenceChanged)
	{
		if (UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
		{
			Settings->SetSaveProgressEnabled(bOriginalPreference);
		}
		bPreferenceChanged = false;
	}
}

void UUEGT2ProgressSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	RestorePreference();
	Super::Deinitialize();
}

bool UUEGT2ProgressSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason)
{
	if (!bCondition) { Finish(false, Reason); }
	return bCondition;
}

void UUEGT2ProgressSmokeSubsystem::Finish(bool bSuccess, const FString& Reason)
{
	if (bFinished) { return; }
	bFinished = true;
	RestorePreference();
	if (bSuccess)
	{
		UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_PROGRESS_SMOKE_COMPLETE phase=%s slot=%s %s"), *Phase, *Slot, *Reason);
	}
	else
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_PROGRESS_SMOKE_FAILED phase=%s slot=%s %s"), *Phase, *Slot, *Reason);
	}
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}

void UUEGT2ProgressSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedSeconds > 120.0) { Finish(false, TEXT("phase did not finish within 120 seconds")); return; }
	if (Step == EStep::Startup && Now - StartedSeconds >= 8.0) { RunPhase(); }
	else if (Step == EStep::MainImage || Step == EStep::PauseImage || Step == EStep::SettingsImage)
	{
		if (bScreenshotComplete)
		{
			if (Step == EStep::MainImage) { LoadAndCheck(); }
			else if (Step == EStep::PauseImage)
			{
				AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(GetWorld()->GetFirstPlayerController());
				if (!Check(PC != nullptr, TEXT("controller disappeared before settings capture"))) { return; }
				PC->ShowSettingsPage(3);
				BeginCapture(EStep::SettingsImage, TEXT("03_ProgressSetting.png"));
			}
			else { BeginClockCheck(); }
		}
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.5)
		{
			bScreenshotRequested = true;
			PendingFile = CaptureFile;
			FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("menu screenshot callback timed out")); }
	}
	else if (Step == EStep::LiveClock)
	{
		ClockElapsed += DeltaTime;
		LargestClockFrame = FMath::Max(LargestClockFrame, DeltaTime);
		if (ClockElapsed >= 3.0f) { CheckClock(); }
	}
}

void UUEGT2ProgressSmokeSubsystem::RunPhase()
{
	using namespace UEGT2ProgressSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("player, life, calendar, progress or known landmark is unavailable"))) { return; }
	UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	if (!Check(Settings != nullptr, TEXT("project settings unavailable"))) { return; }
	bOriginalPreference = Settings->GetSaveProgressEnabled();
	bPreferenceChanged = true;
	Settings->SetSaveProgressEnabled(true);
	if (!Check(C.Progress->IsEnabled(), TEXT("progress feature is unavailable in this build"))) { return; }

	if (Phase == TEXT("NewVisit") && bNewVisitWorld)
	{
		FSlotBytes After;
		After.Read(Slot);
		if (!Check(BeforeNewVisit.Equals(After), TEXT("New Visit changed the existing checkpoint"))) { return; }
		if (!Check(C.PC->GetMenuState() == EUEGT2MenuState::None && C.Life->GetTrade() == EUEGT2NPCRole::Villager
			&& FMath::IsNearlyEqual(C.Life->GetPurse().Coins, UEGT2StartingCoins(EUEGT2NPCRole::Villager), 0.001f)
			&& C.Life->GetNeeds().Energy > 0.95f && C.Life->GetNeeds().Fed > 0.95f
			&& C.Director->GetDayIndex() == 0 && AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == 0
			&& FVector::Dist2D(C.Player->GetActorLocation(), C.Start->GetActorLocation()) < 100.0,
			TEXT("New Visit did not reset player, calendar, discoveries and menu"))) { return; }
		bNewVisitRequested = false;
		Finish(true, TEXT("fresh world verified; previous checkpoint preserved"));
		return;
	}

	C.PC->ShowPauseMenu();
	C.Player->GetCharacterMovement()->StopMovementImmediately();
	if (Phase == TEXT("Write") || Phase == TEXT("NewVisit"))
	{
		if (!Check(C.Life->RestoreProgress(SavedNeeds(), FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith)
			&& C.Director->RestoreCalendar(SavedDay, SavedHour, EUEGT2Weather::Cloudy), TEXT("cannot set checkpoint test state"))) { return; }
		C.Player->SetActorLocation(C.SavedLocation(), false, nullptr, ETeleportType::TeleportPhysics);
		C.PC->SetControlRotation(SavedView);
		ClearLandmarks(GetWorld());
		C.Landmark->SetDiscovered(true);
		if (Phase == TEXT("Write"))
		{
			FSlotBytes Before;
			Before.Read(Slot);
			if (!Check(!Before.HasCheckpoint(), TEXT("temporary checkpoint unexpectedly existed before Write"))) { return; }
			if (!Check(C.PC->SaveProgress(), TEXT("controller Save Progress failed"))) { return; }
			FSlotBytes Written;
			Written.Read(Slot);
			if (!Check(Written.HasCheckpoint() && C.PC->HasSavedProgress(), TEXT("Save Progress produced no readable checkpoint"))) { return; }
			Finish(true, TEXT("pause-menu save contains nonuniform player state and discovery"));
		}
		else
		{
			BeforeNewVisit.Read(Slot);
			if (!Check(BeforeNewVisit.HasCheckpoint(), TEXT("New Visit needs the existing checkpoint"))) { return; }
			bNewVisitRequested = true;
			Step = EStep::WaitingForTravel;
			C.PC->StartPlaying();
		}
		return;
	}

	if (!Check(C.Progress->HasSavedProgress(), TEXT("new process cannot find the previous checkpoint"))) { return; }
	FUEGT2NPCNeeds Different;
	Different.Energy = 0.15f;
	Different.Fed = 0.25f;
	Different.Relief = 0.35f;
	Different.Company = 0.45f;
	if (!Check(C.Life->RestoreProgress(Different, FUEGT2Purse(3.25f), EUEGT2NPCRole::Villager)
		&& C.Director->RestoreCalendar(1, 5.0f, EUEGT2Weather::Storm), TEXT("cannot mutate pre-load state"))) { return; }
	ClearLandmarks(GetWorld());
	C.PC->SetControlRotation(FRotator(0.0, -45.0, 0.0));
	if (Phase == TEXT("Read"))
	{
		C.PC->ShowMainMenu();
		if (!CaptureDirectory.IsEmpty()) { BeginCapture(EStep::MainImage, TEXT("01_Continue.png")); }
		else { LoadAndCheck(); }
		return;
	}

	FSlotBytes Before, After;
	Before.Read(Slot);
	const FVector Location = C.Player->GetActorLocation();
	Settings->SetSaveProgressEnabled(false);
	if (!Check(!C.Progress->IsEnabled() && !C.PC->HasSavedProgress() && !C.PC->SaveProgress() && !C.PC->ContinueProgress(),
		TEXT("player off switch allowed progress operations"))) { return; }
	After.Read(Slot);
	if (!Check(Before.Equals(After) && NeedsEqual(C.Life->GetNeeds(), Different, 0.0001f)
		&& FMath::IsNearlyEqual(C.Life->GetPurse().Coins, 3.25f, 0.0001f)
		&& C.Player->GetActorLocation().Equals(Location, 0.001)
		&& C.Director->GetDayIndex() == 1 && FMath::IsNearlyEqual(C.Director->GetHour(), 5.0f, 0.0001f)
		&& C.PC->GetMenuState() == EUEGT2MenuState::Pause && !C.Landmark->IsDiscovered(),
		TEXT("disabled progress changed files or live state"))) { return; }
	// Slot was validated as our exact GUID-prefixed fixture before any I/O.
	for (const TCHAR* Suffix : { TEXT("_A"), TEXT("_B") })
	{
		const FString PhysicalSlot = Slot + Suffix;
		if (UGameplayStatics::DoesSaveGameExist(PhysicalSlot, 0))
		{
			if (!Check(UGameplayStatics::DeleteGameInSlot(PhysicalSlot, 0), TEXT("cannot remove owned smoke checkpoint"))) { return; }
		}
	}
	Finish(true, TEXT("off switch preserved live state and checkpoint bytes; temporary slots removed"));
}

void UUEGT2ProgressSmokeSubsystem::LoadAndCheck()
{
	using namespace UEGT2ProgressSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && C.PC->ContinueProgress(), TEXT("controller Continue failed"))) { return; }
	if (!Check(NeedsEqual(C.Life->GetNeeds(), SavedNeeds(), 0.0001f)
		&& FMath::IsNearlyEqual(C.Life->GetPurse().Coins, 137.625f, 0.0001f) && C.Life->GetTrade() == EUEGT2NPCRole::Smith
		&& C.Player->GetActorLocation().Equals(C.SavedLocation(), 1.0) && C.PC->GetControlRotation().Equals(SavedView, 0.01)
		&& C.Director->GetDayIndex() == SavedDay && FMath::IsNearlyEqual(C.Director->GetHour(), SavedHour, 0.0001f)
		&& C.Sky->GetWeather() == EUEGT2Weather::Cloudy && C.Landmark->IsDiscovered()
		&& AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == 1 && C.PC->GetMenuState() == EUEGT2MenuState::None,
		TEXT("cross-process checkpoint did not restore position, view, needs, purse, trade, calendar or landmark"))) { return; }
	C.PC->ShowPauseMenu();
	if (!Check(C.PC->ContinueProgress() && AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == 1,
		TEXT("repeated Continue duplicated discoveries"))) { return; }
	C.PC->ShowPauseMenu();
	if (!CaptureDirectory.IsEmpty()) { BeginCapture(EStep::PauseImage, TEXT("02_SaveProgress.png")); }
	else { BeginClockCheck(); }
}

void UUEGT2ProgressSmokeSubsystem::BeginClockCheck()
{
	UEGT2ProgressSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("world disappeared before live clock check"))) { return; }
	ClockNeeds = C.Life->GetNeeds();
	ClockPurse = C.Life->GetPurse();
	ClockStartHour = C.Director->GetHour();
	C.Sky->SetDayLengthMinutes(4.0f);
	C.Sky->SetDayNightCycleEnabled(true);
	C.PC->CloseMenu();
	Step = EStep::LiveClock;
}

void UUEGT2ProgressSmokeSubsystem::CheckClock()
{
	using namespace UEGT2ProgressSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("world disappeared during live clock check"))) { return; }
	C.PC->ShowPauseMenu();
	const float Hours = FMath::Fmod(C.Director->GetHour() - ClockStartHour + 24.0f, 24.0f);
	FUEGT2NPCNeeds Expected = ClockNeeds;
	FUEGT2Purse ExpectedPurse = ClockPurse;
	UEGT2AdvanceLife(Hours, EUEGT2Activity::Idle, EUEGT2NPCRole::Smith, Expected, ExpectedPurse);
	// Director, sky and the component's 10Hz tick can straddle a frame. Allow
	// that scheduling difference, while rejecting a frozen or discontinuous clock.
	const float Tolerance = FMath::Max(0.002f, 0.02f * (LargestClockFrame + 0.15f));
	if (!Check(Hours > 0.05f && Hours < 2.0f && C.Life->GetNeeds().Fed < ClockNeeds.Fed
		&& NeedsEqual(C.Life->GetNeeds(), Expected, Tolerance)
		&& FMath::IsNearlyEqual(C.Life->GetPurse().Coins, ExpectedPurse.Coins, 0.001f)
		&& C.Director->GetDayIndex() == SavedDay, TEXT("loaded needs no longer follow the real world clock"))) { return; }
	Finish(true, FString::Printf(TEXT("roundtrip, duplicate-load and live-clock checks passed; hours=%.4f"), Hours));
}

void UUEGT2ProgressSmokeSubsystem::BeginCapture(EStep NextStep, const TCHAR* FileName)
{
	Step = NextStep;
	StepStartedSeconds = FPlatformTime::Seconds();
	CaptureFile = FPaths::Combine(CaptureDirectory, FileName);
	bScreenshotRequested = false;
	bScreenshotComplete = false;
}

void UUEGT2ProgressSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty()) { return; }
	if (!Check(Width > 0 && Height > 0 && Bitmap.Num() == Width * Height, TEXT("invalid progress menu screenshot"))) { return; }
	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot write progress menu screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Progress menu screenshot: %s"), *PendingFile);
	PendingFile.Reset();
	bScreenshotComplete = true;
}
