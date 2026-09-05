#include "Diagnostics/UEGT2SurveySmokeSubsystem.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputKeyEventArgs.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "Widgets/Text/STextBlock.h"

namespace UEGT2SurveySmoke
{
	const FName SquareId(TEXT("fairhaven_square"));
	const FName HarbourId(TEXT("fairhaven_harbour"));
	const FName JournalName(TEXT("Journal"));

	AUEGT2Landmark* FindLandmark(UWorld* World, FName Id)
	{
		for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
		{
			if (It->GetPersistentId() == Id) { return *It; }
		}
		return nullptr;
	}

	void Key(AUEGT2PlayerController* PC, FKey Key, EInputEvent Event)
	{
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(PC->GetPlatformUserId());
		// Feed a key, not an action value: the real Enhanced Input mapping must
		// resolve the rebound key and call the controller's action binding.
		PC->InputKey(FInputKeyEventArgs(nullptr, Device, Key, Event, FPlatformTime::Cycles64()));
	}

	bool HasDiscoveries(UWorld* World, UUEGT2SurveySubsystem* Survey, int32 Count)
	{
		int32 Listed = 0;
		for (const FUEGT2SurveyEntry& Entry : Survey->GetEntries()) { Listed += Entry.bDiscovered ? 1 : 0; }
		return Listed == Count && AUEGT2Landmark::GetDiscoveredCount(World) == Count;
	}

	TSharedPtr<SWidget> FindButton(const TSharedRef<SWidget>& Widget, const FText& Caption, int32& Budget, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return nullptr; }
		FChildren* Children = Widget->GetChildren();
		if (Widget->GetType() == TEXT("SButton") && Widget->IsEnabled() && Children->Num() == 1)
		{
			const TSharedRef<SWidget> Label = Children->GetChildAt(0);
			if (Label->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Label)->GetText().ToString()
				== Caption.ToString()) { return Widget; }
		}
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindButton(Children->GetChildAt(Index), Caption, Budget, Depth + 1)) { return Found; }
		}
		return nullptr;
	}
}

bool UUEGT2SurveySmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke")) && Super::ShouldCreateSubsystem(Outer);
}

bool UUEGT2SurveySmokeSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game;
}

TStatId UUEGT2SurveySmokeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2SurveySmokeSubsystem, STATGROUP_Tickables);
}

void UUEGT2SurveySmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRequested = FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke"));
	StartedSeconds = FPlatformTime::Seconds();
	FString UserDirectory;
	FString OtherPhase;
	FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory);
	FPaths::NormalizeDirectoryName(UserDirectory);
	RunId = FPaths::GetCleanFilename(UserDirectory);
	FGuid Guid;
	FString ExpectedUserDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/SurveySmoke"), RunId));
	FPaths::NormalizeDirectoryName(ExpectedUserDirectory);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && !FPaths::IsRelative(UserDirectory)
		&& UserDirectory.Equals(ExpectedUserDirectory, ESearchCase::IgnoreCase), TEXT("expected an isolated packaged Saved/SurveySmoke/<guid> UserDir"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
		&& !UUEGT2CaptureSubsystem::IsFlySoakRequested() && !FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), OtherPhase),
		TEXT("survey smoke cannot share another diagnostic run"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2SurveyCapture="), CaptureDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth);
	FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(!FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId,
			TEXT("capture directory must be absolute and belong to this run"))) { return; }
		if (!Check(IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("cannot create survey capture directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2SurveySmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Survey smoke starting: run=%s resolution=%dx%d"), *RunId, ExpectedWidth, ExpectedHeight);
}

void UUEGT2SurveySmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	RestorePreferences();
	Super::Deinitialize();
}

void UUEGT2SurveySmokeSubsystem::RestorePreferences()
{
	if (!bPreferencesChanged) { return; }
	if (UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		Settings->SetSaveProgressEnabled(bOriginalSavePreference);
		Settings->SetSurveyJournalEnabled(bOriginalSurveyPreference);
		Settings->SetKeyOverride(UEGT2SurveySmoke::JournalName, OriginalJournalKey);
	}
	if (UUEGT2SurveySubsystem* Survey = UUEGT2SurveySubsystem::Get(GetWorld())) { Survey->bFeatureEnabled = bOriginalFeatureEnabled; }
	bPreferencesChanged = false;
}

bool UUEGT2SurveySmokeSubsystem::Check(bool bCondition, const TCHAR* Reason)
{
	if (!bCondition) { Finish(false, Reason); }
	return bCondition;
}

void UUEGT2SurveySmokeSubsystem::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; }
	bFinished = true;
	RestorePreferences();
	if (bSuccess) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_SURVEY_SMOKE_COMPLETE run=%s %s"), *RunId, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SURVEY_SMOKE_FAILED run=%s %s"), *RunId, Reason); }
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}

void UUEGT2SurveySmokeSubsystem::SetStep(EStep NextStep)
{
	Step = NextStep;
	StepStartedSeconds = FPlatformTime::Seconds();
}

void UUEGT2SurveySmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedSeconds > 120.0) { Finish(false, TEXT("survey smoke exceeded 120 seconds")); return; }
	if (Step == EStep::Startup)
	{
		if (Now - StartedSeconds >= 8.0) { StartCheck(); }
		return;
	}
	const bool bImageStep = Step == EStep::EmptyImage || Step == EStep::SurveyedImage || Step == EStep::TrackingImage
		|| Step == EStep::PauseImage || Step == EStep::SettingsImage;
	if (bImageStep)
	{
		if (bScreenshotComplete) { Advance(); }
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.5)
		{
			bScreenshotRequested = true;
			PendingFile = CaptureFile;
			FScreenshotRequest::RequestScreenshot(true);
		}
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("survey screenshot callback timed out")); }
	}
	else if (Now - StepStartedSeconds >= 0.5) { Advance(); }
}

void UUEGT2SurveySmokeSubsystem::StartCheck()
{
	using namespace UEGT2SurveySmoke;
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(World->GetFirstPlayerController());
	UUEGT2SurveySubsystem* Survey = UUEGT2SurveySubsystem::Get(World);
	UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(World);
	if (!Check(PC && PC->GetPawn() && PC->GetInputConfig() && PC->GetInputConfig()->JournalAction && Survey && Settings && Progress,
		TEXT("player, input, survey or settings unavailable"))) { return; }
	if (!Check(PC->GetMenuState() == EUEGT2MenuState::None && FindLandmark(World, SquareId) && FindLandmark(World, HarbourId),
		TEXT("expected gameplay and known generated landmarks"))) { return; }
	bOriginalSavePreference = Settings->GetSaveProgressEnabled();
	bOriginalSurveyPreference = Settings->GetSurveyJournalEnabled();
	bOriginalFeatureEnabled = Survey->bFeatureEnabled;
	OriginalJournalKey = Settings->GetKeyOverride(JournalName);
	bPreferencesChanged = true;
	Settings->SetSaveProgressEnabled(false);
	Settings->SetSurveyJournalEnabled(true);
	Settings->SetKeyOverride(JournalName, EKeys::K);
	PC->RebuildInputMappings();
	if (!Check(Survey->IsEnabled() && PC->IsSurveyJournalEnabled() && !Progress->IsEnabled(), TEXT("feature gates do not match smoke preferences"))) { return; }
	for (TActorIterator<AUEGT2Landmark> It(World); It; ++It) { It->SetDiscovered(false); }
	Survey->ClearTracking();
	if (!Check(UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Journal) == EKeys::K && HasDiscoveries(World, Survey, 0),
		TEXT("journal rebind or empty roster failed"))) { return; }
	SetStep(EStep::MappingsReady);
}

bool UUEGT2SurveySmokeSubsystem::CloseWithSlateKey(FKey Key)
{
	if (!Check(FSlateApplication::IsInitialized(), TEXT("Slate application unavailable"))) { return false; }
	const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
	const bool bHandled = FSlateApplication::Get().ProcessKeyDownEvent(Event);
	FSlateApplication::Get().ProcessKeyUpEvent(Event);
	const AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(GetWorld()->GetFirstPlayerController());
	return Check(bHandled && PC && !PC->IsSurveyJournalOpen() && PC->GetMenuState() == EUEGT2MenuState::None && !GetWorld()->IsPaused(),
		TEXT("focused Slate journal did not close to gameplay with the rebound key"));
}

void UUEGT2SurveySmokeSubsystem::Advance()
{
	using namespace UEGT2SurveySmoke;
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(World->GetFirstPlayerController());
	UUEGT2SurveySubsystem* Survey = UUEGT2SurveySubsystem::Get(World);
	UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	if (!Check(PC && PC->GetPawn() && Survey && Settings, TEXT("survey context disappeared"))) { return; }
	FUEGT2SurveyDirection Direction;
	switch (Step)
	{
	case EStep::MappingsReady:
		Key(PC, EKeys::J, IE_Pressed);
		SetStep(EStep::OldKey);
		break;
	case EStep::OldKey:
		Key(PC, EKeys::J, IE_Released);
		if (!Check(!PC->IsSurveyJournalOpen(), TEXT("old J binding still opens journal after rebind"))) { return; }
		Key(PC, EKeys::K, IE_Pressed);
		SetStep(EStep::OpenEmpty);
		break;
	case EStep::OpenEmpty:
		Key(PC, EKeys::K, IE_Released);
		if (!Check(PC->IsSurveyJournalOpen() && World->IsPaused() && HasDiscoveries(World, Survey, 0),
			TEXT("Enhanced Input rebound K did not open an empty paused journal"))) { return; }
		BeginCapture(EStep::EmptyImage, TEXT("01_Empty.png"));
		break;
	case EStep::EmptyImage:
		if (!CloseWithSlateKey()) { return; }
		FindLandmark(World, SquareId)->SetDiscovered(true);
		FindLandmark(World, HarbourId)->SetDiscovered(true);
		Key(PC, EKeys::K, IE_Pressed);
		SetStep(EStep::OpenSurveyed);
		break;
	case EStep::OpenSurveyed:
		Key(PC, EKeys::K, IE_Released);
		if (!Check(PC->IsSurveyJournalOpen() && HasDiscoveries(World, Survey, 2), TEXT("surveyed roster was not reopened through input"))) { return; }
		BeginCapture(EStep::SurveyedImage, TEXT("02_Surveyed.png"));
		break;
	case EStep::SurveyedImage:
		if (!Check(Survey->TrackLandmark(HarbourId) && Survey->GetTrackedLandmarkId() == HarbourId && HasDiscoveries(World, Survey, 2),
			TEXT("tracking changed discoveries or refused surveyed harbour"))) { return; }
		if (!CloseWithSlateKey()) { return; }
		if (!Check(Survey->GetTrackedDirection(PC->GetPawn()->GetActorLocation(), PC->GetControlRotation().Yaw, Direction)
			&& Direction.Id == HarbourId && Direction.DistanceMetres > 10.0f && !Direction.Name.IsEmpty(),
			TEXT("tracked harbour has no world HUD direction after closing journal"))) { return; }
		BeginCapture(EStep::TrackingImage, TEXT("03_Tracking.png"));
		break;
	case EStep::TrackingImage:
		// Show the complete combined-feature pause layout without performing IO.
		// The unique UserDir remains a second barrier around real player saves.
		Settings->SetSaveProgressEnabled(true);
		PC->ShowPauseMenu();
		if (!Check(PC->GetMenuState() == EUEGT2MenuState::Pause && !PC->IsSurveyJournalOpen(), TEXT("pause root did not open"))) { return; }
		BeginCapture(EStep::PauseImage, TEXT("04_PauseRoot.png"));
		break;
	case EStep::PauseImage:
		PC->ShowSettingsPage(3);
		BeginCapture(EStep::SettingsImage, TEXT("05_Settings.png"));
		break;
	case EStep::SettingsImage:
		Settings->SetSaveProgressEnabled(false);
		Settings->SetKeyOverride(JournalName, EKeys::Enter);
		PC->RebuildInputMappings();
		PC->ShowPauseMenu();
		SetStep(EStep::EnterMappings);
		break;
	case EStep::EnterMappings:
	{
		if (!Check(UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Journal) == EKeys::Enter,
			TEXT("Enter rebind did not become effective"))) { return; }
		if (!Check(PC->GetMenuState() == EUEGT2MenuState::Pause && !PC->IsSurveyJournalOpen(),
			TEXT("expected pause root before focused-button journal shortcut"))) { return; }
		const TSharedPtr<SWidget> Menu = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Check(Menu.IsValid() && Menu->GetType() == TEXT("SUEGT2Menu"), TEXT("pause root did not receive keyboard focus"))) { return; }
		int32 Budget = 512;
		const TSharedPtr<SWidget> Button = FindButton(Menu.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "Settings", "Settings"), Budget);
		if (!Check(Button.IsValid() && FSlateApplication::Get().SetKeyboardFocus(Button, EFocusCause::Navigation),
			TEXT("could not focus the real pause-root Settings button"))) { return; }
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Check(Focused == Button && Focused->GetType() == TEXT("SButton"), TEXT("pause-root Settings button lacks keyboard focus"))) { return; }
		const FKeyEvent Event(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
		const bool bHandled = FSlateApplication::Get().ProcessKeyDownEvent(Event);
		FSlateApplication::Get().ProcessKeyUpEvent(Event);
		if (!Check(bHandled && PC->IsSurveyJournalOpen() && World->IsPaused()
			&& Survey->GetTrackedLandmarkId() == HarbourId && HasDiscoveries(World, Survey, 2),
			TEXT("rebound Enter activated the focused pause-root button instead of opening the journal"))) { return; }
		UE_LOG(LogUEGT2Diag, Log, TEXT("Survey smoke: rebound Enter opened the journal from the focused pause-root Settings button."));
		if (!CloseWithSlateKey(EKeys::Enter)) { return; }
		Key(PC, EKeys::Enter, IE_Pressed);
		SetStep(EStep::OpenEnter);
		break;
	}
	case EStep::OpenEnter:
	{
		Key(PC, EKeys::Enter, IE_Released);
		if (!Check(PC->IsSurveyJournalOpen() && World->IsPaused() && Survey->GetTrackedLandmarkId() == HarbourId,
			TEXT("Enhanced Input Enter did not reopen the tracked journal"))) { return; }
		const TSharedPtr<SWidget> Menu = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Check(Menu.IsValid() && Menu->GetType() == TEXT("SUEGT2Menu"), TEXT("journal menu did not receive keyboard focus"))) { return; }
		int32 Budget = 512;
		const TSharedPtr<SWidget> Button = FindButton(Menu.ToSharedRef(), NSLOCTEXT("UEGT2SurveyJournal", "Track", "Track"), Budget);
		if (!Check(Button.IsValid() && FSlateApplication::Get().SetKeyboardFocus(Button, EFocusCause::Navigation),
			TEXT("could not focus a real enabled Track button"))) { return; }
		SetStep(EStep::FocusedEnter);
		break;
	}
	case EStep::FocusedEnter:
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Check(Focused.IsValid() && Focused->GetType() == TEXT("SButton"), TEXT("Track button lost keyboard focus"))) { return; }
		if (!CloseWithSlateKey(EKeys::Enter)) { return; }
		if (!Check(Survey->GetTrackedLandmarkId() == HarbourId && HasDiscoveries(World, Survey, 2)
			&& !UUEGT2ProgressSubsystem::Get(World)->IsEnabled(), TEXT("Enter activated Track instead of closing, or saving remained enabled"))) { return; }
		UE_LOG(LogUEGT2Diag, Log, TEXT("Survey smoke: rebound Enter closed from a focused Track button without activating it."));
		Settings->SetSurveyJournalEnabled(false);
		Settings->ApplyNonResolutionSettings();
		Key(PC, EKeys::Enter, IE_Pressed);
		SetStep(EStep::PlayerOff);
		break;
	}
	case EStep::PlayerOff:
		Key(PC, EKeys::Enter, IE_Released);
		if (!Check(!Survey->IsEnabled() && PC->IsSurveyJournalAvailable() && !PC->IsSurveyJournalOpen()
			&& !Survey->TrackLandmark(HarbourId) && !Survey->GetTrackedDirection(PC->GetPawn()->GetActorLocation(), 0.0f, Direction)
			&& Survey->GetTrackedLandmarkId().IsNone() && AUEGT2Landmark::GetDiscoveredCount(World) == 2,
			TEXT("player off switch did not block journal and tracking while preserving discoveries"))) { return; }
		Settings->SetSurveyJournalEnabled(true);
		Survey->bFeatureEnabled = false;
		Settings->ApplyNonResolutionSettings();
		Key(PC, EKeys::Enter, IE_Pressed);
		SetStep(EStep::HardOff);
		break;
	case EStep::HardOff:
		Key(PC, EKeys::Enter, IE_Released);
		if (!Check(!Survey->IsAvailable() && !PC->IsSurveyJournalAvailable() && !PC->IsSurveyJournalOpen()
			&& !Survey->TrackLandmark(HarbourId) && !Survey->GetTrackedDirection(PC->GetPawn()->GetActorLocation(), 0.0f, Direction)
			&& AUEGT2Landmark::GetDiscoveredCount(World) == 2 && !UUEGT2ProgressSubsystem::Get(World)->IsEnabled(),
			TEXT("hard gate did not block journal and tracking while preserving discoveries"))) { return; }
		Finish(true, TEXT("rebound input, Slate close, discoveries, HUD guidance and both off switches verified"));
		break;
	default: break;
	}
}

void UUEGT2SurveySmokeSubsystem::BeginCapture(EStep NextStep, const TCHAR* FileName)
{
	SetStep(NextStep);
	CaptureFile = FPaths::Combine(CaptureDirectory, FileName);
	bScreenshotRequested = false;
	bScreenshotComplete = CaptureDirectory.IsEmpty();
}

void UUEGT2SurveySmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty() || bFinished) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Bitmap.Num() == Width * Height,
		TEXT("survey screenshot resolution did not match request"))) { return; }
	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot write survey screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Survey screenshot: %s"), *PendingFile);
	PendingFile.Reset();
	bScreenshotComplete = true;
}
