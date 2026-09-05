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
	DevMode,
	SurveyJournal,
	Rest,
	Services,
};

/** Dev mode tab. */
enum class EUEGT2DevTab : uint8
{
	Player,
	World,
	Life,
	Display,
	Teleport,
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
	/** Stable focus while the separate autosave action becomes available. */
	TSharedPtr<SWidget> GetMainInitialFocusWidget() const
	{
		return MenuState == EUEGT2MenuState::Main && Page == EUEGT2MenuPage::Root
			? MainInitialFocus.Pin() : nullptr;
	}

	/** Jump straight to the settings screen on a given tab. */
	void OpenSettings(int32 TabIndex);
	void OpenSurveyJournal();
	bool IsSurveyJournalOpen() const { return Page == EUEGT2MenuPage::SurveyJournal; }
	/** Attach the page and return its preferred initial focus target. */
	TSharedPtr<SWidget> OpenRestPanel(AUEGT2Amenity* Bed);
	bool IsRestPanelOpen() const { return Page == EUEGT2MenuPage::Rest; }
	TSharedPtr<SWidget> OpenServicesGuide();
	bool IsServicesGuideOpen() const { return Page == EUEGT2MenuPage::Services; }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	TSharedRef<SWidget> BuildRoot();
	TSharedRef<SWidget> BuildSurveyJournal();
	TSharedRef<SWidget> BuildRestPanel();
	TSharedRef<SWidget> BuildServicesGuide();
	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> BuildGraphicsTab();
	TSharedRef<SWidget> BuildAudioTab();
	TSharedRef<SWidget> BuildControlsTab();
	TSharedRef<SWidget> BuildGameplayTab();

	TSharedRef<SWidget> BuildDevMode();
	TSharedRef<SWidget> BuildDevPlayerTab();
	TSharedRef<SWidget> BuildDevWorldTab();
	TSharedRef<SWidget> BuildDevLifeTab();
	TSharedRef<SWidget> BuildDevDisplayTab();
	TSharedRef<SWidget> BuildDevTeleportTab();

	void Rebuild();
	void GoToPage(EUEGT2MenuPage Page);
	void SelectTab(EUEGT2SettingsTab Tab);
	void SelectDevTab(EUEGT2DevTab InDevTab);
	void ApplyAndSave(bool bResolutionToo = false);

	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	TWeakObjectPtr<AUEGT2Amenity> RestBed;
	TWeakPtr<SWidget> RestInitialFocus;
	TWeakPtr<SWidget> ServicesInitialFocus;
	TWeakPtr<SWidget> MainInitialFocus;
	EUEGT2MenuState MenuState = EUEGT2MenuState::Main;
	EUEGT2MenuPage Page = EUEGT2MenuPage::Root;
	EUEGT2SettingsTab Tab = EUEGT2SettingsTab::Graphics;
	EUEGT2DevTab DevTab = EUEGT2DevTab::Player;

	/** Set while waiting for the player to press a key for a rebind. */
	TOptional<EUEGT2InputSlot> PendingRebind;

	TSharedPtr<SBorder> ContentHost;
};
