// Fairhaven (UEGT2) - canvas HUD: crosshair, interaction prompt, diagnostics.
//
// Canvas rather than UMG because the HUD is small, always-on and benefits from
// being pure code. The menus use Slate; see SUEGT2Menu.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UEGT2HUD.generated.h"

class AUEGT2Character;
class AUEGT2PlayerController;

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2HUD : public AHUD
{
	GENERATED_BODY()

public:
	AUEGT2HUD();

	virtual void DrawHUD() override;

	/** Show a short message in the centre-bottom of the screen for a few seconds. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|HUD")
	void ShowMessage(const FText& Message, float Duration = 3.5f);

private:
	void DrawCrosshair(float CentreX, float CentreY, bool bHasFocus);
	void DrawPrompt(float CentreX, float CentreY);
	void DrawMessage(float CentreX, float ScreenHeight);
	void DrawDiagnostics(AUEGT2Character* Explorer);
	/** Top-right banner listing whatever dev mode currently has switched on. */
	void DrawDevStatus(float ScreenWidth);

	FText CurrentMessage;
	float MessageExpiry = 0.0f;

	/** Smoothed frame time so the readout is legible. */
	float SmoothedDeltaMs = 16.6f;
};
