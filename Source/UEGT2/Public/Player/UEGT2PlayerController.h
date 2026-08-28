// Fairhaven (UEGT2) - input wiring and menu ownership.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UEGT2PlayerController.generated.h"

class AUEGT2NPCActor;
class SUEGT2Dialogue;
class SUEGT2Menu;
class UUEGT2InputConfig;

UENUM(BlueprintType)
enum class EUEGT2MenuState : uint8
{
	/** Playing: no menu, cursor captured. */
	None,
	/** Front end shown over a scenic view of the world. */
	Main,
	/** In-game pause menu. */
	Pause,
};

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AUEGT2PlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu") void ShowMainMenu();
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu") void ShowPauseMenu();
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu") void CloseMenu();
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu") void ToggleMenu();

	/** Leave the front end and take control of the explorer. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu") void StartPlaying();

	/** Drop back to the front end without leaving the level. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu") void ReturnToMainMenu();

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu") void QuitGame();

	UFUNCTION(BlueprintPure, Category = "UEGT2|Menu")
	EUEGT2MenuState GetMenuState() const { return MenuState; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Menu")
	bool IsMenuOpen() const { return MenuState != EUEGT2MenuState::None; }

	// ---- Conversation ------------------------------------------------------
	/**
	 * Start talking to somebody.
	 *
	 * The world keeps running behind it, unlike the pause menu: the point of
	 * asking whether somebody is hungry is that they are getting hungrier while
	 * you ask, and the needs bars in the panel move while you watch.
	 */
	void OpenDialogue(AUEGT2NPCActor* NPC);

	void CloseDialogue();

	bool IsDialogueOpen() const { return DialoguePartner.IsValid(); }

	/**
	 * Ask the open conversation one question, by topic index.
	 *
	 * Exists so the capture tour can fill a transcript without a human clicking
	 * through it, and so a future console command can drive the same path.
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Dialogue")
	void AskDialogueTopic(int32 Topic);

	/** Rebuild input mappings after a control rebind. */
	void RebuildInputMappings();

	/**
	 * Open the settings screen on a given tab (0 graphics, 1 audio, 2 controls,
	 * 3 gameplay). Used by the automated menu capture; harmless otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Menu")
	void ShowSettingsPage(int32 TabIndex);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Input")
	UUEGT2InputConfig* GetInputConfig() const { return InputConfig; }

	/** Toggled by the Diagnostics action and read by the HUD. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Diagnostics")
	bool IsDiagnosticsVisible() const { return bDiagnosticsVisible; }

	/** Set it directly, so the dev menu and the F3 key agree. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Diagnostics")
	void SetDiagnosticsVisible(bool bVisible) { bDiagnosticsVisible = bVisible; }

private:
	void ApplyMenuState(EUEGT2MenuState NewState);
	/** Bind the pawn's own actions. Safe to call from either possession or
	 *  input setup, whichever happens second. */
	void BindPawnActions();
	void OnMenuAction();
	void OnDiagnosticsAction();
	void EnsureMenuWidget();
	void EnsureDialogueWidget();
	void ApplyDialogueInputMode();

	UPROPERTY(Transient) TObjectPtr<UUEGT2InputConfig> InputConfig = nullptr;

	TSharedPtr<SUEGT2Menu> MenuWidget;
	TSharedPtr<SUEGT2Dialogue> DialogueWidget;
	TWeakObjectPtr<AUEGT2NPCActor> DialoguePartner;
	EUEGT2MenuState MenuState = EUEGT2MenuState::None;
	bool bDiagnosticsVisible = false;
	bool bPawnActionsBound = false;
};
