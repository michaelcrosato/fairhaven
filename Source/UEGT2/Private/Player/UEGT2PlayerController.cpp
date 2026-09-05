#include "Player/UEGT2PlayerController.h"

#include "Autosave/UEGT2AutosaveSubsystem.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Rest/UEGT2RestSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "UEGT2LogChannels.h"
#include "NPC/UEGT2NPCActor.h"
#include "UI/SUEGT2Dialogue.h"
#include "UI/SUEGT2Menu.h"
#include "Widgets/SWeakWidget.h"

AUEGT2PlayerController::AUEGT2PlayerController()
{
	bShowMouseCursor = false;
	// The pause menu has to keep working while the world is paused.
	bShouldPerformFullTickWhenPaused = true;
}

void AUEGT2PlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		Settings->LoadSettings();
		Settings->ApplyNonResolutionSettings();
	}

	EnsureMenuWidget();
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	const bool bNewJourney = Progress && Progress->ConsumeNewJourneyRequest();

	// Boot into the front end unless something explicitly skips it: smoke tests
	// and screenshot tours want the world, not the menu.
	if ((bNewJourney || FParse::Param(FCommandLine::Get(), TEXT("UEGT2SkipMenu"))
			|| UUEGT2CaptureSubsystem::IsCaptureRequested()
			|| UUEGT2CaptureSubsystem::IsWalkSmokeRequested())
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2CaptureMenu")))
	{
		ApplyMenuState(EUEGT2MenuState::None);
	}
	else
	{
		ApplyMenuState(EUEGT2MenuState::Main);
	}
}

void AUEGT2PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bAutoWalkPressedThisTick = false;
	if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->CancelAutoWalk(); }
	if (UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld()))
	{
		Progress->SetJourneyActive(false);
	}
	SetPlayerConversing(false);
	DialoguePartner.Reset();
	if (DialogueWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DialogueWidget.ToSharedRef());
	}
	DialogueWidget.Reset();
	if (MenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	}
	MenuWidget.Reset();
	Super::EndPlay(EndPlayReason);
}

void AUEGT2PlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	bAutoWalkPressedThisTick = false;
	// Controller input and view rotation run before character movement. Adding
	// this in the pawn's later actor tick would queue forward input for next frame.
	if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->ApplyAutoWalkInput(); }
}

void AUEGT2PlayerController::FlushPressedKeys()
{
	bAutoWalkPressedThisTick = false;
	if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->CancelAutoWalk(); }
	Super::FlushPressedKeys();
}

bool AUEGT2PlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Enhanced Input can rebuild a Started edge from an OS repeat after a flush
	// or pause. Its synthetic release also clears the engine's ignore-held flag.
	// Require the real press here, before engine reconciliation, for this action.
	if ((Params.Event == IE_Pressed || Params.Event == IE_DoubleClick) && !Params.IsSimulatedInput()
		&& GetWorld() && !GetWorld()->IsPaused()
		&& MenuState == EUEGT2MenuState::None && !IsDialogueOpen()
		&& (Params.Key == UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::ToggleAutoWalk)
			|| Params.Key == EKeys::Gamepad_RightThumbstick))
	{
		bAutoWalkPressedThisTick = true;
	}
	return Super::InputKey(Params);
}

void AUEGT2PlayerController::OnAutoWalkAction()
{
	if (!bAutoWalkPressedThisTick) { return; }
	bAutoWalkPressedThisTick = false;
	if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->ToggleAutoWalk(); }
}

void AUEGT2PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputConfig)
	{
		InputConfig = NewObject<UUEGT2InputConfig>(this, TEXT("InputConfig"));
		InputConfig->Initialize();
	}

	RebuildInputMappings();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogUEGT2Player, Error, TEXT("Enhanced Input component missing; controls will not work."));
		return;
	}

	Input->BindAction(InputConfig->MenuAction, ETriggerEvent::Started, this, &AUEGT2PlayerController::OnMenuAction);
	Input->BindAction(InputConfig->DiagnosticsAction, ETriggerEvent::Started, this, &AUEGT2PlayerController::OnDiagnosticsAction);
	Input->BindAction(InputConfig->JournalAction, ETriggerEvent::Started, this, &AUEGT2PlayerController::ToggleSurveyJournal);
	// A quick release/repress can stay Triggered across an input tick. The raw
	// press ticket, rather than an engine-generated Started edge, toggles once.
	Input->BindAction(InputConfig->AutoWalkAction, ETriggerEvent::Triggered, this, &AUEGT2PlayerController::OnAutoWalkAction);

	// SetupInputComponent usually runs BEFORE the pawn is possessed, so the
	// pawn's own actions are bound from whichever of the two happens second.
	BindPawnActions();
}

void AUEGT2PlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	BindPawnActions();
}

void AUEGT2PlayerController::OnUnPossess()
{
	bAutoWalkPressedThisTick = false;
	if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->CancelAutoWalk(); }
	if (GetPawn() && InputComponent)
	{
		// Pawn actions live on this controller's persistent input component.
		// Drop the old pawn's delegates before possession can bind another one.
		InputComponent->ClearBindingsForObject(GetPawn());
	}
	bPawnActionsBound = false;
	Super::OnUnPossess();
}

void AUEGT2PlayerController::BindPawnActions()
{
	if (bPawnActionsBound || !InputConfig)
	{
		return;
	}
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn());
	if (!Input || !Explorer)
	{
		return;
	}
	Explorer->BindInputActions(Input, InputConfig);
	bPawnActionsBound = true;
	UE_LOG(LogUEGT2Player, Log, TEXT("Player input bound to %s."), *Explorer->GetName());
}

void AUEGT2PlayerController::RebuildInputMappings()
{
	bAutoWalkPressedThisTick = false;
	if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->CancelAutoWalk(); }
	if (!InputConfig)
	{
		return;
	}
	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}
	Subsystem->ClearAllMappings();
	if (UInputMappingContext* Context = InputConfig->BuildMappingContext())
	{
		Subsystem->AddMappingContext(Context, 0);
	}
}

void AUEGT2PlayerController::EnsureMenuWidget()
{
	if (MenuWidget.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	MenuWidget = SNew(SUEGT2Menu).Controller(this);
	GEngine->GameViewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 100);
	MenuWidget->SetVisibility(EVisibility::Collapsed);
}

void AUEGT2PlayerController::EnsureDialogueWidget()
{
	if (DialogueWidget.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	DialogueWidget = SNew(SUEGT2Dialogue).Controller(this);
	// Below the menu's z-order: opening the pause menu mid-conversation should
	// put the menu on top, not behind.
	GEngine->GameViewport->AddViewportWidgetContent(DialogueWidget.ToSharedRef(), 60);
	DialogueWidget->SetVisibility(EVisibility::Collapsed);
}

void AUEGT2PlayerController::ApplyDialogueInputMode()
{
	const bool bOpen = DialoguePartner.IsValid();
	bShowMouseCursor = bOpen;
	if (bOpen)
	{
		// UIOnly, but the world is NOT paused: needs keep running down while you
		// talk, which is the whole reason the panel shows them.
		FInputModeUIOnly Mode;
		if (DialogueWidget.IsValid())
		{
			Mode.SetWidgetToFocus(DialogueWidget);
		}
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
	else if (MenuState == EUEGT2MenuState::None)
	{
		SetInputMode(FInputModeGameOnly());
	}
}

void AUEGT2PlayerController::OpenDialogue(AUEGT2NPCActor* NPC)
{
	if (NPC == nullptr || MenuState != EUEGT2MenuState::None)
	{
		return;
	}
	bAutoWalkPressedThisTick = false;
	if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->CancelAutoWalk(); }
	EnsureDialogueWidget();
	DialoguePartner = NPC;
	if (DialogueWidget.IsValid())
	{
		DialogueWidget->SetVisibility(EVisibility::Visible);
		DialogueWidget->SetPartner(NPC);
	}
	ApplyDialogueInputMode();
	SetPlayerConversing(true);
	UE_LOG(LogUEGT2UI, Log, TEXT("Talking to %s."), *NPC->GetDisplayName().ToString());
}

void AUEGT2PlayerController::CloseDialogue()
{
	bAutoWalkPressedThisTick = false;
	SetPlayerConversing(false);
	DialoguePartner.Reset();
	if (DialogueWidget.IsValid())
	{
		DialogueWidget->SetPartner(nullptr);
		DialogueWidget->SetVisibility(EVisibility::Collapsed);
	}
	ApplyDialogueInputMode();
}

void AUEGT2PlayerController::SetPlayerConversing(bool bTalking)
{
	// Company is a need, and standing talking to somebody is how it is
	// answered - for the player exactly as for the person they are talking to,
	// who is in Socialise for the same reason at the same moment.
	if (const AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn()))
	{
		if (UUEGT2NeedsComponent* Life = Explorer->GetLife())
		{
			Life->SetConversing(bTalking);
		}
	}
}

void AUEGT2PlayerController::AskDialogueTopic(int32 Topic)
{
	if (DialogueWidget.IsValid() && DialoguePartner.IsValid()
		&& Topic >= 0 && Topic < (int32)EUEGT2DialogueTopic::Count)
	{
		DialogueWidget->AskTopic((EUEGT2DialogueTopic)Topic);
	}
}

void AUEGT2PlayerController::ApplyMenuState(EUEGT2MenuState NewState)
{
	if (NewState != EUEGT2MenuState::None || MenuState != EUEGT2MenuState::None)
	{
		bAutoWalkPressedThisTick = false;
		if (AUEGT2Character* Explorer = Cast<AUEGT2Character>(GetPawn())) { Explorer->CancelAutoWalk(); }
	}
	if (IsRestPanelOpen()) { UE_LOG(LogUEGT2Rest, Log, TEXT("Sleep panel closed.")); }
	if (IsSurveyJournalOpen() && NewState != EUEGT2MenuState::Pause)
	{
		UE_LOG(LogUEGT2Survey, Log, TEXT("Survey journal closed."));
	}
	if (NewState != EUEGT2MenuState::None && DialoguePartner.IsValid())
	{
		CloseDialogue();
	}

	MenuState = NewState;
	if (UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld()))
	{
		// The front-end camera is presentation, never a checkpoint location.
		if (NewState == EUEGT2MenuState::Main) { Progress->SetJourneyActive(false); }
		else if (NewState == EUEGT2MenuState::None) { Progress->SetJourneyActive(true); }
	}
	EnsureMenuWidget();

	const bool bOpen = (MenuState != EUEGT2MenuState::None);

	if (MenuWidget.IsValid())
	{
		MenuWidget->SetVisibility(bOpen ? EVisibility::Visible : EVisibility::Collapsed);
		if (bOpen)
		{
			MenuWidget->SetMenuState(MenuState);
		}
	}

	// The front end leaves the world running so the view behind stays alive;
	// the pause menu actually pauses.
	SetPause(MenuState == EUEGT2MenuState::Pause);

	bShowMouseCursor = bOpen;
	if (bOpen)
	{
		FInputModeUIOnly Mode;
		if (MenuWidget.IsValid())
		{
			Mode.SetWidgetToFocus(MenuWidget);
			// Main keeps one stable focus target while cached autosave availability
			// changes. Apply this only on opening the menu, never on I/O completion.
			const TSharedPtr<SWidget> InitialFocus = MenuWidget->GetMainInitialFocusWidget();
			if (InitialFocus.IsValid()) { Mode.SetWidgetToFocus(InitialFocus); }
		}
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}

	UE_LOG(LogUEGT2UI, Log, TEXT("Menu state: %s"),
		MenuState == EUEGT2MenuState::None ? TEXT("None") :
		MenuState == EUEGT2MenuState::Main ? TEXT("Main") : TEXT("Pause"));
}

void AUEGT2PlayerController::ShowMainMenu() { ApplyMenuState(EUEGT2MenuState::Main); }
void AUEGT2PlayerController::ShowPauseMenu() { ApplyMenuState(EUEGT2MenuState::Pause); }
void AUEGT2PlayerController::CloseMenu() { ApplyMenuState(EUEGT2MenuState::None); }

bool AUEGT2PlayerController::OpenRestPanel(AUEGT2Amenity* Bed)
{
	UUEGT2RestSubsystem* Rest = UUEGT2RestSubsystem::Get(GetWorld());
	FText Reason;
	if (!Rest || !Rest->CanSleepAt(this, Bed, Reason)) { return false; }
	ShowPauseMenu();
	if (!MenuWidget.IsValid() || !GetWorld()->IsPaused())
	{
		CloseMenu();
		return false;
	}
	const TSharedPtr<SWidget> InitialFocus = MenuWidget->OpenRestPanel(Bed);
	if (InitialFocus.IsValid())
	{
		// Queue this after the page is attached and after ShowPauseMenu's root
		// focus. A full-screen focus rectangle cannot navigate into its buttons.
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(InitialFocus);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
	UE_LOG(LogUEGT2Rest, Log, TEXT("Sleep panel opened."));
	return true;
}

bool AUEGT2PlayerController::IsRestPanelOpen() const
{
	return MenuState == EUEGT2MenuState::Pause && MenuWidget.IsValid() && MenuWidget->IsRestPanelOpen();
}

bool AUEGT2PlayerController::IsRestAvailable() const
{
	const UUEGT2RestSubsystem* Rest = UUEGT2RestSubsystem::Get(GetWorld());
	return Rest && Rest->IsAvailable();
}

void AUEGT2PlayerController::ToggleMenu()
{
	if (MenuState == EUEGT2MenuState::None)
	{
		ShowPauseMenu();
	}
	else if (MenuState == EUEGT2MenuState::Pause)
	{
		CloseMenu();
	}
	// The front end has no "close": the player must choose Play.
}

void AUEGT2PlayerController::OnMenuAction()
{
	ToggleMenu();
}

void AUEGT2PlayerController::OnDiagnosticsAction()
{
	bDiagnosticsVisible = !bDiagnosticsVisible;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Diagnostics overlay %s."),
		bDiagnosticsVisible ? TEXT("shown") : TEXT("hidden"));
}

void AUEGT2PlayerController::ShowSettingsPage(int32 TabIndex)
{
	if (MenuState == EUEGT2MenuState::None)
	{
		ApplyMenuState(EUEGT2MenuState::Main);
	}
	if (MenuWidget.IsValid())
	{
		MenuWidget->OpenSettings(TabIndex);
	}
}

void AUEGT2PlayerController::StartPlaying()
{
	if (UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
		Progress && Progress->IsEnabled())
	{
		// A fresh visit gets a fresh generated world. Keep the old checkpoint
		// until the player explicitly saves the new visit.
		Progress->RequestNewJourney();
		UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
		return;
	}
	UE_LOG(LogUEGT2UI, Log, TEXT("Starting play from the front end."));
	CloseMenu();
}

bool AUEGT2PlayerController::SaveProgress()
{
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	return Progress && Progress->SaveProgress(this);
}

void AUEGT2PlayerController::ToggleSurveyJournal()
{
	if (IsSurveyJournalOpen()) { CloseMenu(); return; }
	if (MenuState == EUEGT2MenuState::Main || !IsSurveyJournalEnabled()) { return; }
	ShowPauseMenu();
	if (MenuWidget.IsValid())
	{
		MenuWidget->OpenSurveyJournal();
		UE_LOG(LogUEGT2Survey, Log, TEXT("Survey journal opened."));
	}
}

bool AUEGT2PlayerController::IsSurveyJournalOpen() const
{
	return MenuState == EUEGT2MenuState::Pause && MenuWidget.IsValid() && MenuWidget->IsSurveyJournalOpen();
}

bool AUEGT2PlayerController::IsSurveyJournalEnabled() const
{
	const UUEGT2SurveySubsystem* Survey = UUEGT2SurveySubsystem::Get(GetWorld());
	return Survey && Survey->IsEnabled();
}

bool AUEGT2PlayerController::IsSurveyJournalAvailable() const
{
	const UUEGT2SurveySubsystem* Survey = UUEGT2SurveySubsystem::Get(GetWorld());
	return Survey && Survey->IsAvailable();
}

bool AUEGT2PlayerController::ContinueProgress()
{
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	if (!Progress || !Progress->LoadProgress(this)) { return false; }
	CloseDialogue();
	CloseMenu();
	return true;
}

bool AUEGT2PlayerController::IsProgressEnabled() const
{
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	return Progress && Progress->IsEnabled();
}

bool AUEGT2PlayerController::IsProgressAvailable() const
{
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	return Progress && Progress->IsAvailable();
}

bool AUEGT2PlayerController::HasSavedProgress() const
{
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	return Progress && Progress->HasSavedProgress();
}

FText AUEGT2PlayerController::GetProgressStatus() const
{
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	return Progress ? Progress->GetStatusText() : FText::GetEmpty();
}

bool AUEGT2PlayerController::ContinueAutosavedProgress()
{
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	if (MenuState != EUEGT2MenuState::Main || !IsAutosaveEnabled()
		|| !Progress || !Progress->LoadAutosavedProgress(this)) { return false; }
	CloseDialogue();
	CloseMenu();
	return true;
}

bool AUEGT2PlayerController::IsAutosaveAvailable() const
{
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	return Autosave && Autosave->IsAvailable();
}

bool AUEGT2PlayerController::IsAutosaveEnabled() const
{
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	return Autosave && Autosave->IsEnabled();
}

FUEGT2AutosaveStatus AUEGT2PlayerController::GetAutosaveStatus() const
{
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	return Progress ? Progress->GetAutosaveStatus() : FUEGT2AutosaveStatus{};
}

void AUEGT2PlayerController::RefreshAutosaveAvailability()
{
	// EnsureMenuWidget constructs a hidden Main page before BeginPlay selects
	// the actual menu state. That construction must not start an availability read.
	if (MenuState != EUEGT2MenuState::Main || !IsAutosaveEnabled()) { return; }
	if (UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld()))
	{
		Progress->RefreshAutosaveAvailability(this);
	}
}

void AUEGT2PlayerController::ReturnToMainMenu()
{
	if (UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld()))
	{
		Progress->SetJourneyActive(false);
	}
	// Put the explorer back at the start so the front end reads cleanly.
	if (APawn* CurrentPawn = GetPawn())
	{
		TArray<AActor*> Starts;
		UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), Starts);
		if (Starts.Num() > 0 && Starts[0])
		{
			CurrentPawn->SetActorLocationAndRotation(
				Starts[0]->GetActorLocation(), Starts[0]->GetActorRotation());
			SetControlRotation(Starts[0]->GetActorRotation());
		}
	}
	ShowMainMenu();
}

void AUEGT2PlayerController::QuitGame()
{
	UE_LOG(LogUEGT2UI, Log, TEXT("Quit requested from the menu."));
	if (UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		Settings->SaveSettings();
	}
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}
