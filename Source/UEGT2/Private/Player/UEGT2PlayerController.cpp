#include "Player/UEGT2PlayerController.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Settings/UEGT2GameUserSettings.h"
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

	// Boot into the front end unless something explicitly skips it: smoke tests
	// and screenshot tours want the world, not the menu.
	if ((FParse::Param(FCommandLine::Get(), TEXT("UEGT2SkipMenu"))
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
	if (MenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	}
	MenuWidget.Reset();
	Super::EndPlay(EndPlayReason);
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
	EnsureDialogueWidget();
	DialoguePartner = NPC;
	if (DialogueWidget.IsValid())
	{
		DialogueWidget->SetVisibility(EVisibility::Visible);
		DialogueWidget->SetPartner(NPC);
	}
	ApplyDialogueInputMode();
	UE_LOG(LogUEGT2UI, Log, TEXT("Talking to %s."), *NPC->GetDisplayName().ToString());
}

void AUEGT2PlayerController::CloseDialogue()
{
	DialoguePartner.Reset();
	if (DialogueWidget.IsValid())
	{
		DialogueWidget->SetPartner(nullptr);
		DialogueWidget->SetVisibility(EVisibility::Collapsed);
	}
	ApplyDialogueInputMode();
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
	if (NewState != EUEGT2MenuState::None && DialoguePartner.IsValid())
	{
		CloseDialogue();
	}

	MenuState = NewState;
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
	UE_LOG(LogUEGT2UI, Log, TEXT("Starting play from the front end."));
	CloseMenu();
}

void AUEGT2PlayerController::ReturnToMainMenu()
{
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
