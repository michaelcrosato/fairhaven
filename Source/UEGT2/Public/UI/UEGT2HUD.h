// Fairhaven (UEGT2) - canvas HUD: crosshair, interaction prompt, diagnostics.
//
// Canvas rather than UMG because the HUD is small, always-on and benefits from
// being pure code. The menus use Slate; see SUEGT2Menu.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UI/UEGT2HUDLayout.h"
#include "UEGT2HUD.generated.h"

class AUEGT2Character;
class AUEGT2PlayerController;
struct FUEGT2SpeechBubble;
struct FUEGT2HUDLife;
struct FUEGT2HUDSurvey;

UCLASS(Config = Game, ClassGroup = "UEGT2")
class UEGT2_API AUEGT2HUD : public AHUD
{
	GENERATED_BODY()

public:
	AUEGT2HUD();

	virtual void DrawHUD() override;
	/** Independent maintainer switch; disabling keeps the player's size choice. */
	UPROPERTY(Config) bool bHudScalingEnabled = true;

	/** Show a short message in the centre-bottom of the screen for a few seconds. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|HUD")
	void ShowMessage(const FText& Message, float Duration = 3.5f);

private:
	void DrawCrosshair(float CentreX, float CentreY, bool bHasFocus);
	FBox2D DrawPrompt(float CentreX, float CentreY);
	FBox2D DrawMessage(const TArray<FBox2D>& BottomPanels);
	FBox2D DrawAutoWalkIndicator(const AUEGT2Character* Explorer);
	FUEGT2HUDLife PrepareLife(AUEGT2Character* Explorer, float MaxWidth);
	FUEGT2HUDSurvey PrepareSurvey(AUEGT2PlayerController* PC);
	void DrawDiagnostics(AUEGT2Character* Explorer);
	/**
	 * Top-left: the clock, the date and what it is doing outside.
	 *
	 * Always on rather than a dev readout, because a world with a day, a
	 * calendar and weather in it is hard to read without one - you cannot tell
	 * a wet morning in Thawmoon from a wet evening in Harvest by looking.
	 */
	void DrawAlmanac(float ScreenWidth);

	/** Top-right banner listing whatever dev mode currently has switched on. */
	void DrawDevStatus(float ScreenWidth);
	/** Bottom-right: straight-line direction to the selected surveyed place. */
	void DrawSurveyTracking(const FUEGT2HUDSurvey& Survey);

	/**
	 * Bottom-left: the player's trade, their purse, what they are doing and
	 * the four needs driving them.
	 *
	 * The same four an NPC has, drawn from the same struct. It is the only
	 * place in the game that says out loud that the player is an inhabitant
	 * too - without it, hunger is an invisible number that eventually slows
	 * your legs down for no stated reason.
	 */
	void DrawLife(const FUEGT2HUDLife& Life);

	/** One labelled 0..1 bar. Returns the height it used. */
	float DrawNeedBar(const FString& Label, float Value, float X, float Y, float Width);

	/**
	 * The NPC speech bubbles.
	 *
	 * Canvas rather than a world-space widget on purpose, and for the same
	 * reason the rest of this HUD is: no binary UI assets, and the text stays
	 * legible at any distance because it never scales with perspective. What it
	 * costs is that everything below has to be laid out by hand.
	 */
	void DrawSpeechBubbles(const TArray<FBox2D>& PlayerPanels, const FBox2D& AutoWalkBounds);
	/**
	 * Lay out and draw one bubble, pushing it clear of any already placed.
	 *
	 * ``Placed`` is the rectangles drawn so far this frame. Two people standing
	 * together and talking is the most common case there is, and without this
	 * their bubbles land on top of each other and neither can be read.
	 */
	void DrawOneBubble(const FUEGT2SpeechBubble& Bubble, TArray<FBox2D>& Placed, bool bFitBounds);

	/** Split Text into lines no wider than MaxWidth in the given font. */
	void WrapText(const FString& Text, class UFont* Font, float Scale, float MaxWidth,
		TArray<FString>& OutLines, float& OutWidth) const;

	/** A filled rectangle with the corners knocked off, for the bubble body. */
	void DrawRoundedRect(const FLinearColor& Colour, float X, float Y, float W, float H,
		float Corner);
	/** One Canvas conversion for all player panels; no global Canvas transform. */
	void MeasureHudText(const FString& Text, float& W, float& H, class UFont* Font, float Scale = 1.0f) const;
	void DrawHudText(const FString& Text, FLinearColor Colour, float X, float Y, class UFont* Font,
		float Scale = 1.0f, bool bScalePosition = false);
	void DrawHudRect(FLinearColor Colour, float X, float Y, float W, float H);
	void DrawHudLine(float X1, float Y1, float X2, float Y2, FLinearColor Colour, float Thickness);
	void LayoutText(const FString& Text, class UFont* Font, float MaxWidth, TArray<FString>& Lines,
		float& Width, float& LineHeight, int32 MaxLines = 4) const;
	FUEGT2HUDLayout HudLayout;
	float LastLoggedScale = -1.0f;
	bool bLastLoggedGate = true;

	FText CurrentMessage;
	float MessageExpiry = 0.0f;

	/** Smoothed frame time so the readout is legible. */
	float SmoothedDeltaMs = 16.6f;

	/** Throttle for the -UEGT2LiveNPCs bubble layout log. */
	float NextBubbleLogTime = 0.0f;
};
