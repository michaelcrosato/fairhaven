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
struct FUEGT2SpeechBubble;

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

	/**
	 * The NPC speech bubbles.
	 *
	 * Canvas rather than a world-space widget on purpose, and for the same
	 * reason the rest of this HUD is: no binary UI assets, and the text stays
	 * legible at any distance because it never scales with perspective. What it
	 * costs is that everything below has to be laid out by hand.
	 */
	void DrawSpeechBubbles();
	/**
	 * Lay out and draw one bubble, pushing it clear of any already placed.
	 *
	 * ``Placed`` is the rectangles drawn so far this frame. Two people standing
	 * together and talking is the most common case there is, and without this
	 * their bubbles land on top of each other and neither can be read.
	 */
	void DrawOneBubble(const FUEGT2SpeechBubble& Bubble, TArray<FBox2D>& Placed);

	/** Split Text into lines no wider than MaxWidth in the given font. */
	void WrapText(const FString& Text, class UFont* Font, float Scale, float MaxWidth,
		TArray<FString>& OutLines, float& OutWidth) const;

	/** A filled rectangle with the corners knocked off, for the bubble body. */
	void DrawRoundedRect(const FLinearColor& Colour, float X, float Y, float W, float H,
		float Corner);

	FText CurrentMessage;
	float MessageExpiry = 0.0f;

	/** Smoothed frame time so the readout is legible. */
	float SmoothedDeltaMs = 16.6f;

	/** Throttle for the -UEGT2LiveNPCs bubble layout log. */
	float NextBubbleLogTime = 0.0f;
};
