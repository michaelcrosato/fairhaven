// Fairhaven (UEGT2) - front end, pause menu and settings, in Slate.
//
// Slate rather than UMG on purpose: the whole UI stays in readable C++ with no
// binary widget assets, which is far easier for future agents to modify safely.
#pragma once

#include "CoreMinimal.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2PlayerController.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SVerticalBox;
class SWidgetSwitcher;

/** Which screen the menu is showing. */
enum class EUEGT2MenuPage : uint8
{
	Root,
	Settings,
};

/** Settings tab. */
enum class EUEGT2SettingsTab : uint8
{
	Graphics,
	Audio,
	Controls,
	Gameplay,
};

class SUEGT2Menu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEGT2Menu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2PlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Switch between front end and pause presentation. */
	void SetMenuState(EUEGT2MenuState InState);

	/** Jump straight to the settings screen on a given tab. */
	void OpenSettings(int32 TabIndex);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	TSharedRef<SWidget> BuildRoot();
	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> BuildGraphicsTab();
	TSharedRef<SWidget> BuildAudioTab();
	TSharedRef<SWidget> BuildControlsTab();
	TSharedRef<SWidget> BuildGameplayTab();

	void Rebuild();
	void GoToPage(EUEGT2MenuPage Page);
	void SelectTab(EUEGT2SettingsTab Tab);
	void ApplyAndSave(bool bResolutionToo = false);

	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	EUEGT2MenuState MenuState = EUEGT2MenuState::Main;
	EUEGT2MenuPage Page = EUEGT2MenuPage::Root;
	EUEGT2SettingsTab Tab = EUEGT2SettingsTab::Graphics;

	/** Set while waiting for the player to press a key for a rebind. */
	TOptional<EUEGT2InputSlot> PendingRebind;

	TSharedPtr<SBorder> ContentHost;
};
