#include "Diagnostics/UEGT2AutosaveSmokeSubsystem.h"

#include "Autosave/UEGT2AutosaveSubsystem.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Kismet/GameplayStatics.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSave.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2AutosaveSmoke
{
	const FName Square(TEXT("fairhaven_square"));
	const FName Harbour(TEXT("fairhaven_harbour"));

	FUEGT2NPCNeeds Needs(int32 Fixture)
	{
		FUEGT2NPCNeeds Value;
		Value.Energy = Fixture == 1 ? 0.73f : 0.36f;
		Value.Fed = Fixture == 1 ? 0.42f : 0.84f;
		Value.Relief = Fixture == 1 ? 0.61f : 0.55f;
		Value.Company = Fixture == 1 ? 0.28f : 0.69f;
		return Value;
	}
	float Coins(int32 Fixture) { return Fixture == 1 ? 137.625f : 219.375f; }
	EUEGT2NPCRole Trade(int32 Fixture) { return Fixture == 1 ? EUEGT2NPCRole::Smith : EUEGT2NPCRole::Baker; }
	int32 Day(int32 Fixture) { return Fixture == 1 ? 7 : 11; }
	float Hour(int32 Fixture) { return Fixture == 1 ? 13.25f : 17.75f; }
	EUEGT2Weather Weather(int32 Fixture) { return Fixture == 1 ? EUEGT2Weather::Cloudy : EUEGT2Weather::Clear; }
	FRotator View(int32 Fixture) { return Fixture == 1 ? FRotator(-9.0, 71.5, 0.0) : FRotator(-6.0, -25.5, 0.0); }
	bool Equal(const FUEGT2NPCNeeds& A, const FUEGT2NPCNeeds& B)
	{
		return FMath::IsNearlyEqual(A.Energy, B.Energy, 0.0001f) && FMath::IsNearlyEqual(A.Fed, B.Fed, 0.0001f)
			&& FMath::IsNearlyEqual(A.Relief, B.Relief, 0.0001f) && FMath::IsNearlyEqual(A.Company, B.Company, 0.0001f);
	}

	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		AUEGT2Character* Player = nullptr;
		UUEGT2NeedsComponent* Life = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		UUEGT2ProgressSubsystem* Progress = nullptr;
		UUEGT2AutosaveSubsystem* Auto = nullptr;
		UUEGT2GameUserSettings* Settings = nullptr;
		explicit FContext(UWorld* World)
		{
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
			Life = Player ? Player->GetLife() : nullptr;
			Director = UUEGT2NPCDirector::Get(World); Sky = AUEGT2SkyController::Get(World);
			Progress = UUEGT2ProgressSubsystem::Get(World); Auto = UUEGT2AutosaveSubsystem::Get(World);
			Settings = UUEGT2GameUserSettings::Get();
		}
		bool IsValid() const { return PC && Player && Life && Life->HasBegunPlay() && Director && Sky && Progress && Auto && Settings; }
	};

	TSharedPtr<SWidget> FindButton(const TSharedRef<SWidget>& Widget, const FText& Caption, bool bVisibleOnly, int32& Budget, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || (bVisibleOnly && !Widget->GetVisibility().IsVisible())) { return nullptr; }
		FChildren* Children = Widget->GetChildren();
		if (Widget->GetType() == TEXT("SButton") && Children->Num() == 1)
		{
			const TSharedRef<SWidget> Label = Children->GetChildAt(0);
			if (Label->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Label)->GetText().ToString() == Caption.ToString()) { return Widget; }
		}
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindButton(Children->GetChildAt(Index), Caption, bVisibleOnly, Budget, Depth + 1)) { return Found; }
		}
		return nullptr;
	}

	FString DescribeWidget(const TSharedPtr<SWidget>& Widget)
	{
		return Widget.IsValid() ? FString::Printf(TEXT("%s@%p visibility=%s enabled=%d"),
			*Widget->GetTypeAsString(), Widget.Get(), *Widget->GetVisibility().ToString(), Widget->IsEnabled()) : TEXT("<expired>");
	}
}

bool UUEGT2AutosaveSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Requested;
	return FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke="), Requested) && Super::ShouldCreateSubsystem(Outer);
}
bool UUEGT2AutosaveSmokeSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const { return WorldType == EWorldType::Game; }
TStatId UUEGT2AutosaveSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2AutosaveSmokeSubsystem, STATGROUP_Tickables); }

void UUEGT2AutosaveSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRequested = FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke="), Phase);
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSlot="), Slot);
	StartedSeconds = LastTickSeconds = FPlatformTime::Seconds();
	FString Suffix = Slot;
	FGuid Guid;
	if (!Check(Suffix.RemoveFromStart(TEXT("UEGT2_ProgressSmoke_"), ESearchCase::CaseSensitive)
		&& FGuid::ParseExact(Suffix, EGuidFormats::Digits, Guid), TEXT("expected a unique diagnostic progress slot"))) { return; }
	RunId = Suffix;
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(&InWorld);
	if (!Check(Progress && Progress->IsAutosaveSmoke() && (Phase == TEXT("Write") || Phase == TEXT("Read") || Phase == TEXT("Disabled")),
		TEXT("autosave diagnostic phase/UserDir failed central isolation validation"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
		&& !UUEGT2CaptureSubsystem::IsFlySoakRequested() && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke")), TEXT("autosave smoke cannot share another diagnostic"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveCapture="), CaptureDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth); FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(Phase == TEXT("Read") && !FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId
			&& IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("capture needs Read and an owned absolute directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2AutosaveSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Autosave smoke starting: phase=%s slot=%s"), *Phase, *Slot);
}

void UUEGT2AutosaveSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle); RestorePreferences(); Super::Deinitialize();
}
void UUEGT2AutosaveSmokeSubsystem::RestorePreferences()
{
	if (!bPreferencesChanged) { return; }
	UEGT2AutosaveSmoke::FContext C(GetWorld());
	if (C.Settings) { C.Settings->SetSaveProgressEnabled(bOriginalSavePreference); C.Settings->SetAutosaveEnabled(bOriginalAutoPreference); }
	if (C.Auto) { C.Auto->bFeatureEnabled = bOriginalFeatureEnabled; }
	if (C.Sky) { C.Sky->SetDayNightCycleEnabled(bOriginalClockEnabled); }
	bPreferencesChanged = false;
}
bool UUEGT2AutosaveSmokeSubsystem::Check(bool bCondition, const TCHAR* Reason) { if (!bCondition) { Finish(false, Reason); } return bCondition; }
void UUEGT2AutosaveSmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true; RestorePreferences();
	if (bSuccess) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_AUTOSAVE_SMOKE_COMPLETE phase=%s slot=%s worst_live_frame_ms=%.3f %s"), *Phase, *Slot, WorstLiveFrameMs, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_AUTOSAVE_SMOKE_FAILED phase=%s slot=%s %s"), *Phase, *Slot, Reason); }
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}
void UUEGT2AutosaveSmokeSubsystem::SetStep(EStep NextStep) { Step = NextStep; StepStartedSeconds = FPlatformTime::Seconds(); BusyStartedSeconds = 0.0; }

void UUEGT2AutosaveSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	const double FrameMs = (Now - LastTickSeconds) * 1000.0; LastTickSeconds = Now;
	if (Now - StartedSeconds > 150.0) { Finish(false, TEXT("autosave phase exceeded 150 seconds")); return; }
	if (Step == EStep::Startup) { if (Now - StartedSeconds >= 8.0) { StartPhase(); } return; }
	UEGT2AutosaveSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("autosave world context disappeared"))) { return; }
	const FUEGT2AutosaveStatus Status = C.Progress->GetAutosaveStatus();
	if (Step == EStep::FirstWrite || Step == EStep::SecondWrite)
	{
		WorstLiveFrameMs = FMath::Max(WorstLiveFrameMs, FrameMs);
		if (Status.bBusy && BusyStartedSeconds == 0.0) { BusyStartedSeconds = Now; }
		if (!Status.bBusy && Status.SuccessfulWrites > WritesBefore)
		{
			UE_LOG(LogUEGT2Diag, Log, TEXT("Autosave periodic completion: writes=%llu play_wait_ms=%.3f observed_busy_ms=%.3f worst_frame_ms=%.3f"),
				static_cast<unsigned long long>(Status.SuccessfulWrites), (Now - StepStartedSeconds) * 1000.0,
				BusyStartedSeconds > 0.0 ? (Now - BusyStartedSeconds) * 1000.0 : 0.0, WorstLiveFrameMs);
			Advance();
		}
		return;
	}
	if (Step == EStep::ReadAvailability || Step == EStep::ReadLayout)
	{
		if (FSlateApplication::Get().GetKeyboardFocusedWidget() != InitialFocus.Pin())
		{
			LogMainRow(TEXT("focus changed during availability/layout"));
			Finish(false, TEXT("async availability refresh moved Main focus")); return;
		}
		if (Step == EStep::ReadAvailability && !Status.bBusy && Status.bAvailable)
		{
			// Status completes through the core ticker. Visibility/IsEnabled are
			// cached Slate attributes, updated by its next prepass, not this world
			// subsystem tick. Let normal layout consume the result without forcing
			// attributes, rebuilding the page or touching keyboard focus.
			LogMainRow(TEXT("availability ready before Slate layout"));
			SetStep(EStep::ReadLayout);
		}
		else if (Step == EStep::ReadLayout && Now - StepStartedSeconds >= 0.3)
		{
			LogMainRow(TEXT("availability after settled Slate layout"));
			if (!Check(!Status.bBusy && Status.bAvailable, TEXT("cached autosave availability changed while settling Main layout"))) { return; }
			Advance();
		}
		return;
	}
	if (Step == EStep::MainImage || Step == EStep::SettingsImage)
	{
		if (bScreenshotComplete) { Advance(); }
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.5)
		{
			bScreenshotRequested = true; PendingFile = CaptureFile; FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("autosave screenshot callback timed out")); }
		return;
	}
	if (Step == EStep::PlayerOff || Step == EStep::HardOff || Step == EStep::ProgressOff)
	{
		if (!Check(!Status.bBusy && Status.SuccessfulWrites == WritesBefore, TEXT("off gate admitted an autosave operation"))) { return; }
	}
	const double Wait = Step == EStep::AutoFocus ? 0.3 : 3.0;
	if (Now - StepStartedSeconds >= Wait && !Status.bBusy) { Advance(); }
}

void UUEGT2AutosaveSmokeSubsystem::CaptureFiles(bool bIncludeAuto, TArray<FUEGT2AutosaveSmokeFile>& Out) const
{
	Out.Reset();
	const TCHAR* Suffixes[] = { TEXT("_A"), TEXT("_B"), TEXT("_Auto_A"), TEXT("_Auto_B") };
	for (int32 Index = 0; Index < (bIncludeAuto ? 4 : 2); ++Index)
	{
		FUEGT2AutosaveSmokeFile File; File.Slot = Slot + Suffixes[Index];
		File.bExists = UGameplayStatics::LoadDataFromSlot(File.Bytes, File.Slot, 0); Out.Add(MoveTemp(File));
	}
}
bool UUEGT2AutosaveSmokeSubsystem::CheckFiles(const TArray<FUEGT2AutosaveSmokeFile>& Baseline, const TCHAR* Reason)
{
	for (const FUEGT2AutosaveSmokeFile& File : Baseline)
	{
		TArray<uint8> Bytes;
		const bool bExists = UGameplayStatics::LoadDataFromSlot(Bytes, File.Slot, 0);
		if (!Check(bExists == File.bExists && Bytes == File.Bytes, *FString::Printf(TEXT("%s: %s"), Reason, *File.Slot))) { return false; }
	}
	return true;
}

bool UUEGT2AutosaveSmokeSubsystem::SeedPlayer(int32 Fixture)
{
	using namespace UEGT2AutosaveSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && C.Life->RestoreProgress(Needs(Fixture), FUEGT2Purse(Coins(Fixture)), Trade(Fixture))
		&& C.Director->RestoreCalendar(Day(Fixture), Hour(Fixture), Weather(Fixture)), TEXT("cannot seed fractional autosave fixture"))) { return false; }
	C.Player->GetCharacterMovement()->StopMovementImmediately(); C.PC->SetControlRotation(View(Fixture));
	int32 Discovered = 0;
	for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It)
	{
		const bool bDiscover = It->GetPersistentId() == Square || (Fixture == 2 && It->GetPersistentId() == Harbour);
		It->SetDiscovered(bDiscover); Discovered += bDiscover ? 1 : 0;
	}
	return Check(Discovered == Fixture, TEXT("known autosave landmarks are missing"));
}

bool UUEGT2AutosaveSmokeSubsystem::ReadLatestAuto(int64 Sequence, int32 Fixture, FString& OutSlot)
{
	using namespace UEGT2AutosaveSmoke;
	ExpectedSave = nullptr;
	TSet<FName> Ids;
	for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It) { Ids.Add(It->GetPersistentId()); }
	for (const TCHAR* Suffix : { TEXT("_Auto_A"), TEXT("_Auto_B") })
	{
		TArray<uint8> Bytes;
		const FString Physical = Slot + Suffix;
		if (!UGameplayStatics::LoadDataFromSlot(Bytes, Physical, 0)) { continue; }
		FText Reason;
		TStrongObjectPtr<UUEGT2ProgressSave> Candidate(UUEGT2ProgressSave::Decode(Bytes, Reason));
		if (Candidate.IsValid() && Candidate->Validate(UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()), Ids, Reason)
			&& (!ExpectedSave || Candidate->Sequence > ExpectedSave->Sequence)) { ExpectedSave = Candidate.Get(); OutSlot = Physical; }
	}
	return Check(ExpectedSave && ExpectedSave->Sequence == Sequence && Equal(ExpectedSave->Needs, Needs(Fixture))
		&& FMath::IsNearlyEqual(ExpectedSave->Purse.Coins, Coins(Fixture), 0.0001f) && ExpectedSave->Trade == Trade(Fixture)
		&& ExpectedSave->DayIndex == Day(Fixture) && FMath::IsNearlyEqual(ExpectedSave->Hour, Hour(Fixture), 0.0001f)
		&& ExpectedSave->Weather == Weather(Fixture) && ExpectedSave->ViewRotation.Equals(View(Fixture), 0.01)
		&& ExpectedSave->DiscoveredLandmarks.Num() == Fixture && ExpectedSave->DiscoveredLandmarks.Contains(Square)
		&& (Fixture == 1 || ExpectedSave->DiscoveredLandmarks.Contains(Harbour)), TEXT("newest valid auto does not contain the expected exact fixture"));
}

void UUEGT2AutosaveSmokeSubsystem::StartPhase()
{
	using namespace UEGT2AutosaveSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid() && C.Progress->IsAutosaveSmoke() && FMath::IsNearlyEqual(C.Auto->GetIntervalSeconds(), 2.0f)
		&& FSlateApplication::IsInitialized(), TEXT("autosave context or validated two-second interval unavailable"))) { return; }
	bOriginalSavePreference = C.Settings->GetSaveProgressEnabled(); bOriginalAutoPreference = C.Settings->GetAutosaveEnabled();
	bOriginalFeatureEnabled = C.Auto->bFeatureEnabled; bOriginalClockEnabled = C.Sky->IsDayNightCycleEnabled(); bPreferencesChanged = true;
	C.Settings->SetSaveProgressEnabled(true); C.Settings->SetAutosaveEnabled(false); C.Sky->SetDayNightCycleEnabled(false);
	if (!Check(C.Progress->IsEnabled() && C.Auto->IsAvailable() && !C.Progress->GetAutosaveStatus().bBusy, TEXT("autosave fixture gates or operation state invalid"))) { return; }
	if (Phase == TEXT("Write"))
	{
		CaptureFiles(true, AllFiles);
		for (const FUEGT2AutosaveSmokeFile& File : AllFiles) { if (!Check(!File.bExists, TEXT("isolated write phase unexpectedly found an existing checkpoint"))) { return; } }
		C.PC->ShowPauseMenu();
		C.Life->SetCoins(40.125f);
		if (!Check(C.PC->SaveProgress(), TEXT("cannot create first manual checkpoint"))) { return; }
		C.Life->SetCoins(55.875f);
		if (!Check(C.PC->SaveProgress(), TEXT("cannot rotate manual checkpoint"))) { return; }
		CaptureFiles(false, ManualFiles);
		if (!Check(ManualFiles[0].bExists && ManualFiles[1].bExists && ManualFiles[0].Bytes != ManualFiles[1].Bytes, TEXT("manual pair fixture was not created"))) { return; }
		if (!SeedPlayer(1)) { return; }
		C.PC->CloseMenu(); C.Settings->SetAutosaveEnabled(true);
		WritesBefore = C.Progress->GetAutosaveStatus().SuccessfulWrites;
		SetStep(EStep::FirstWrite);
	}
	else
	{
		CaptureFiles(false, ManualFiles); CaptureFiles(true, AllFiles);
		FString Physical;
		if (!ReadLatestAuto(1, 1, Physical)) { return; }
		if (Phase == TEXT("Read")) { BeginReadMenu(); }
		else
		{
			WritesBefore = C.Progress->GetAutosaveStatus().SuccessfulWrites;
			if (!Check(!C.Auto->IsEnabled() && !C.Progress->RequestAutosave(C.PC), TEXT("player autosave off admitted a request"))) { return; }
			C.Progress->RefreshAutosaveAvailability(C.PC);
			SetStep(EStep::PlayerOff);
		}
	}
}

bool UUEGT2AutosaveSmokeSubsystem::BeginReadMenu()
{
	using namespace UEGT2AutosaveSmoke;
	FContext C(GetWorld());
	if (!Check(C.PC->GetMenuState() == EUEGT2MenuState::Main && !C.Progress->IsJourneyActive(GetWorld()), TEXT("Read must begin at the actual Main menu"))) { return false; }
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	TSharedPtr<SWidget> Root = Focused;
	for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
	if (!Check(Root.IsValid() && Root->GetType() == TEXT("SUEGT2Menu"), TEXT("initial Main focus has no menu ancestor"))) { return false; }
	int32 Budget = 512;
	const TSharedPtr<SWidget> NewVisit = FindButton(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "NewVisit", "New Visit"), true, Budget);
	if (!Check(NewVisit.IsValid() && Focused == NewVisit, TEXT("Main did not naturally focus New Visit"))) { return false; }
	MenuRoot = Root; InitialFocus = Focused;
	Budget = 512;
	AutoButton = FindButton(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "ContinueAutosave", "Continue Autosave"), false, Budget);
	if (!Check(AutoButton.IsValid(), TEXT("Main omitted its pre-existing asynchronous auto row")) || !CheckMainRow(false)) { return false; }
	// Deliberately differ from the stored checkpoint before the explicit load.
	if (!SeedPlayer(2)) { return false; }
	C.Settings->SetAutosaveEnabled(true);
	GenerationBefore = C.Progress->GetJourneyGeneration();
	C.PC->RefreshAutosaveAvailability();
	if (!Check(C.Progress->GetAutosaveStatus().bBusy, TEXT("Main availability refresh did not start asynchronously"))) { return false; }
	SetStep(EStep::ReadAvailability);
	return true;
}

bool UUEGT2AutosaveSmokeSubsystem::CheckMainRow(bool bVisible)
{
	const TSharedPtr<SWidget> Root = MenuRoot.Pin();
	if (!Check(Root.IsValid() && InitialFocus.IsValid(), TEXT("Main widget identity was destroyed during availability read"))) { return false; }
	int32 Budget = 512;
	const TSharedPtr<SWidget> Found = UEGT2AutosaveSmoke::FindButton(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "ContinueAutosave", "Continue Autosave"), true, Budget);
	const bool bMatches = bVisible ? (Found.IsValid() && Found == AutoButton.Pin() && Found->IsEnabled()) : !Found.IsValid();
	if (!bMatches) { LogMainRow(bVisible ? TEXT("expected visible enabled existing auto row") : TEXT("expected hidden auto row")); }
	return Check(bMatches,
		bVisible ? TEXT("existing Main auto row did not become visible and enabled") : TEXT("autosave-off Main row was visible"));
}

void UUEGT2AutosaveSmokeSubsystem::LogMainRow(const TCHAR* Context) const
{
	using namespace UEGT2AutosaveSmoke;
	const FContext C(GetWorld());
	const FUEGT2AutosaveStatus Status = C.Progress ? C.Progress->GetAutosaveStatus() : FUEGT2AutosaveStatus();
	const TSharedPtr<SWidget> Root = MenuRoot.Pin();
	const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
	int32 Budget = 512;
	const TSharedPtr<SWidget> Found = Root.IsValid() ? FindButton(Root.ToSharedRef(),
		NSLOCTEXT("UEGT2Menu", "ContinueAutosave", "Continue Autosave"), true, Budget) : nullptr;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Autosave Main row (%s): caption=Continue Autosave root={%s} focus={%s} initial={%s} existing={%s} visible_match={%s} feature_enabled=%d cached_available=%d cached_busy=%d status=%s budget=%d"),
		Context, *DescribeWidget(Root), *DescribeWidget(Focus), *DescribeWidget(InitialFocus.Pin()),
		*DescribeWidget(AutoButton.Pin()), *DescribeWidget(Found), C.Auto && C.Auto->IsEnabled(),
		Status.bAvailable, Status.bBusy, *Status.Text.ToString(), Budget);
	TSharedPtr<SWidget> Ancestor = AutoButton.Pin();
	for (int32 Depth = 0; Ancestor.IsValid() && Depth < 16; ++Depth)
	{
		UE_LOG(LogUEGT2Diag, Log, TEXT("Autosave Main row ancestor[%d]: %s"), Depth, *DescribeWidget(Ancestor));
		if (Ancestor == Root) { break; }
		Ancestor = Ancestor->GetParentWidget();
	}
}

bool UUEGT2AutosaveSmokeSubsystem::SendGamepadKey(FKey Key)
{
	const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
	const bool bDown = FSlateApplication::Get().ProcessKeyDownEvent(Event);
	const bool bUp = FSlateApplication::Get().ProcessKeyUpEvent(Event);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Autosave smoke gamepad: %s down=%d up=%d"), *Key.ToString(), bDown, bUp);
	return Key != EKeys::Gamepad_FaceButton_Bottom || Check(bDown && bUp, TEXT("Continue Autosave did not accept gamepad A"));
}

bool UUEGT2AutosaveSmokeSubsystem::CheckLoadedState()
{
	using namespace UEGT2AutosaveSmoke;
	FContext C(GetWorld());
	if (!Check(ExpectedSave && C.PC->GetMenuState() == EUEGT2MenuState::None && !GetWorld()->IsPaused()
		&& C.Progress->IsJourneyActive(GetWorld()) && C.Progress->GetJourneyGeneration() > GenerationBefore
		&& Equal(C.Life->GetNeeds(), ExpectedSave->Needs) && FMath::IsNearlyEqual(C.Life->GetPurse().Coins, ExpectedSave->Purse.Coins, 0.0001f)
		&& C.Life->GetTrade() == ExpectedSave->Trade && !C.Life->IsOccupied()
		&& C.Director->GetDayIndex() == ExpectedSave->DayIndex && FMath::IsNearlyEqual(C.Director->GetHour(), ExpectedSave->Hour, 0.0001f)
		&& C.Director->GetWeather() == ExpectedSave->Weather && C.PC->GetControlRotation().Equals(ExpectedSave->ViewRotation, 0.01)
		&& C.Player->GetActorLocation().Equals(ExpectedSave->PlayerLocation, 0.1), TEXT("cross-process auto load did not restore exact state and a new live journey"))) { return false; }
	int32 Discovered = 0;
	for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It)
	{
		const bool bExpected = ExpectedSave->DiscoveredLandmarks.Contains(It->GetPersistentId());
		if (!Check(It->IsDiscovered() == bExpected, TEXT("auto load did not replace landmark discoveries"))) { return false; }
		Discovered += It->IsDiscovered() ? 1 : 0;
	}
	return Check(Discovered == 1, TEXT("auto load duplicated discoveries"));
}

void UUEGT2AutosaveSmokeSubsystem::Advance()
{
	using namespace UEGT2AutosaveSmoke;
	FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("autosave context disappeared"))) { return; }
	FString Physical;
	switch (Step)
	{
	case EStep::FirstWrite:
		if (!ReadLatestAuto(1, 1, Physical) || !CheckFiles(ManualFiles, TEXT("periodic autosave altered manual checkpoint"))) { return; }
		C.PC->ShowPauseMenu(); CaptureFiles(true, AllFiles);
		WritesBefore = C.Progress->GetAutosaveStatus().SuccessfulWrites;
		if (!Check(!C.Auto->CanAutosaveNow(C.PC) && !C.Progress->RequestAutosave(C.PC), TEXT("pause admitted an auto write"))) { return; }
		SetStep(EStep::PauseHold);
		break;
	case EStep::PauseHold:
		if (!Check(C.Progress->GetAutosaveStatus().SuccessfulWrites == WritesBefore, TEXT("paused time triggered autosave")) || !CheckFiles(AllFiles, TEXT("pause changed checkpoint bytes"))) { return; }
		C.PC->ShowMainMenu();
		if (!Check(!C.Progress->IsJourneyActive(GetWorld()) && !C.Auto->CanAutosaveNow(C.PC) && !C.Progress->RequestAutosave(C.PC), TEXT("Main admitted periodic autosaving"))) { return; }
		SetStep(EStep::MainHold);
		break;
	case EStep::MainHold:
		if (!Check(C.Progress->GetAutosaveStatus().SuccessfulWrites == WritesBefore, TEXT("Main time triggered autosave")) || !CheckFiles(AllFiles, TEXT("Main changed checkpoint bytes"))) { return; }
		GenerationBefore = C.Progress->GetJourneyGeneration();
		if (!Check(C.PC->ContinueProgress() && C.Progress->GetJourneyGeneration() > GenerationBefore, TEXT("manual Continue did not replace journey independently"))) { return; }
		if (!SeedPlayer(2)) { return; }
		SetStep(EStep::SecondWrite);
		break;
	case EStep::SecondWrite:
	{
		if (!ReadLatestAuto(2, 2, Physical) || !CheckFiles(ManualFiles, TEXT("rotated auto changed manual bytes"))) { return; }
		C.Settings->SetAutosaveEnabled(false);
		CaptureFiles(true, AllFiles);
		if (!Check(AllFiles[2].bExists && AllFiles[3].bExists, TEXT("autosave did not rotate both physical slots"))) { return; }
		// Only the centrally validated isolated slot is corrupted. The next
		// process must recover the earlier valid auto through normal menu reads.
		const TArray<uint8> Damaged = { 0x55, 0x45, 0x47, 0x54, 0x00 };
		if (!Check(Physical == Slot + TEXT("_Auto_A") || Physical == Slot + TEXT("_Auto_B"), TEXT("refusing to corrupt a non-fixture slot"))
			|| !Check(UGameplayStatics::SaveDataToSlot(Damaged, Physical, 0), TEXT("cannot prepare corrupt-newest fixture"))) { return; }
		if (!ReadLatestAuto(1, 1, Physical) || !CheckFiles(ManualFiles, TEXT("fallback preparation changed manual bytes"))) { return; }
		Finish(true, TEXT("two periodic writes, manual preservation, pause/Main deferral and older-auto recovery fixture verified"));
		break;
	}
	case EStep::ReadLayout:
		if (!CheckMainRow(true) || !CheckFiles(AllFiles, TEXT("availability read rewrote checkpoints"))) { return; }
		UE_LOG(LogUEGT2Diag, Log, TEXT("Autosave smoke: existing Main row appeared asynchronously with New Visit focus unchanged."));
		BeginCapture(EStep::MainImage, TEXT("01_ContinueAutosave.png"));
		break;
	case EStep::MainImage:
		if (!CheckMainRow(true) || !Check(FSlateApplication::Get().GetKeyboardFocusedWidget() == InitialFocus.Pin(), TEXT("Main capture changed initial focus"))) { return; }
		SendGamepadKey(EKeys::Gamepad_DPad_Up); SetStep(EStep::AutoFocus);
		break;
	case EStep::AutoFocus:
		if (!Check(FSlateApplication::Get().GetKeyboardFocusedWidget() == AutoButton.Pin(), TEXT("natural gamepad Up did not reach Continue Autosave"))
			|| !SendGamepadKey(EKeys::Gamepad_FaceButton_Bottom)) { return; }
		C.Sky->SetDayNightCycleEnabled(false);
		if (!CheckLoadedState() || !CheckFiles(ManualFiles, TEXT("Continue Autosave changed manual bytes"))) { return; }
		C.PC->ShowPauseMenu(); C.PC->ShowSettingsPage(3);
		BeginCapture(EStep::SettingsImage, TEXT("02_AutosaveSetting.png"));
		break;
	case EStep::SettingsImage:
		if (!CheckFiles(AllFiles, TEXT("Read phase wrote checkpoints")) || !Check(C.Progress->GetAutosaveStatus().SuccessfulWrites == 0, TEXT("Read phase initiated a periodic write"))) { return; }
		Finish(true, TEXT("async Main row, natural gamepad load, corrupt-newest fallback and exact cross-process restoration verified"));
		break;
	case EStep::PlayerOff:
		if (!CheckFiles(AllFiles, TEXT("player off changed checkpoints"))) { return; }
		C.Settings->SetAutosaveEnabled(true); C.Auto->bFeatureEnabled = false;
		if (!Check(!C.Auto->IsEnabled() && !C.Progress->RequestAutosave(C.PC), TEXT("autosave hard off admitted a request"))) { return; }
		C.Progress->RefreshAutosaveAvailability(C.PC); SetStep(EStep::HardOff);
		break;
	case EStep::HardOff:
		if (!CheckFiles(AllFiles, TEXT("hard off changed checkpoints"))) { return; }
		C.Auto->bFeatureEnabled = true; C.Settings->SetSaveProgressEnabled(false);
		if (!Check(!C.Progress->IsEnabled() && !C.Auto->IsEnabled() && !C.Progress->RequestAutosave(C.PC), TEXT("Save Progress off admitted autosave"))) { return; }
		C.Progress->RefreshAutosaveAvailability(C.PC); SetStep(EStep::ProgressOff);
		break;
	case EStep::ProgressOff:
		if (!CheckFiles(AllFiles, TEXT("parent off changed checkpoints"))) { return; }
		Finish(true, TEXT("player, autosave hard and persistence parent gates preserved both checkpoint channels"));
		break;
	default: break;
	}
}

void UUEGT2AutosaveSmokeSubsystem::BeginCapture(EStep NextStep, const TCHAR* FileName)
{
	SetStep(NextStep); CaptureFile = FPaths::Combine(CaptureDirectory, FileName);
	bScreenshotRequested = false; bScreenshotComplete = CaptureDirectory.IsEmpty();
}
void UUEGT2AutosaveSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty() || bFinished) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Bitmap.Num() == Width * Height, TEXT("autosave screenshot has wrong requested resolution"))) { return; }
	TArray<FColor> Opaque = Bitmap; for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot write autosave screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Autosave screenshot: %s"), *PendingFile); PendingFile.Reset(); bScreenshotComplete = true;
}
