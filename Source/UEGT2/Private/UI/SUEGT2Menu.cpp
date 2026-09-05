#include "SUEGT2Menu.h"
#include "SUEGT2SurveyJournal.h"

#include "Dev/UEGT2DevModeSubsystem.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"

#include "Framework/Application/SlateApplication.h"
#include "Player/UEGT2InputConfig.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Styling/CoreStyle.h"
#include "UEGT2LogChannels.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "UI/UEGT2UIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEGT2Menu"

// ---------------------------------------------------------------------------
// Style: solid tinted boxes and the stock font, so the UI needs no assets.
// ---------------------------------------------------------------------------
namespace UEGT2Menu
{
	// The palette, the boxes and the plain button live in UEGT2UIStyle.h so the
	// conversation panel looks like it belongs to the same game. Only the parts
	// that are the menu's own are defined here.
	using namespace UEGT2UI;

	static const FButtonStyle& TabStyle(bool bSelected)
	{
		static const FButtonStyle Selected = []
		{
			FButtonStyle Result;
			Result.SetNormal(Tinted(FLinearColor(Accent.R, Accent.G, Accent.B, 0.34f)));
			Result.SetHovered(Tinted(FLinearColor(Accent.R, Accent.G, Accent.B, 0.42f)));
			Result.SetPressed(Tinted(FLinearColor(Accent.R, Accent.G, Accent.B, 0.50f)));
			Result.SetNormalPadding(FMargin(14.0f, 7.0f));
			Result.SetPressedPadding(FMargin(14.0f, 7.0f));
			return Result;
		}();
		return bSelected ? Selected : ButtonStyle();
	}

	static TSharedRef<SWidget> MenuButton(const FText& Text, FOnClicked OnClicked, int32 Size = 16)
	{
		return SNew(SButton)
			.ButtonStyle(&ButtonStyle())
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(OnClicked)
			[
				Label(Text, Size, Ink, "Bold")
			];
	}

	/** Label on the left, control on the right, in a fixed-width settings row. */
	static TSharedRef<SWidget> Row(const FText& LabelText, TSharedRef<SWidget> Control)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center).Padding(0, 0, 16, 0)
			[
				Label(LabelText, 13, Ink)
			]
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				Control
			];
	}
}

using namespace UEGT2Menu;

namespace
{
	UUEGT2GameUserSettings* Settings()
	{
		return UUEGT2GameUserSettings::Get();
	}

	/** The dev subsystem for the menu's controller, or null outside a world. */
	UUEGT2DevModeSubsystem* DevOf(const TWeakObjectPtr<AUEGT2PlayerController>& WeakPC)
	{
		const AUEGT2PlayerController* PC = WeakPC.Get();
		return PC ? UUEGT2DevModeSubsystem::Get(PC->GetWorld()) : nullptr;
	}

	/** 13.75 -> "13:45". */
	FText HourText(float Hours)
	{
		const int32 Whole = FMath::Clamp(FMath::FloorToInt(Hours), 0, 23);
		const int32 Minutes = FMath::Clamp(FMath::RoundToInt((Hours - Whole) * 60.0f), 0, 59);
		return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Whole, Minutes));
	}

	FText QualityName(int32 Level)
	{
		switch (Level)
		{
		case 0:  return LOCTEXT("QualityLow", "Low");
		case 1:  return LOCTEXT("QualityMedium", "Medium");
		case 2:  return LOCTEXT("QualityHigh", "High");
		case 3:  return LOCTEXT("QualityEpic", "Epic");
		default: return LOCTEXT("QualityCinematic", "Cinematic");
		}
	}

	FText PercentText(float Value)
	{
		return FText::AsPercent(Value);
	}
}

// ---------------------------------------------------------------------------
// Row builders
// ---------------------------------------------------------------------------
TSharedRef<SWidget> MakeSlider(TFunction<float()> Get, TFunction<void(float)> Set,
	float Min, float Max, TFunction<FText()> Display)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SSlider)
			.Value_Lambda([Get, Min, Max]() { return (Get() - Min) / FMath::Max(Max - Min, KINDA_SMALL_NUMBER); })
			.OnValueChanged_Lambda([Set, Min, Max](float Normalised) { Set(Min + Normalised * (Max - Min)); })
			.SliderBarColor(FSlateColor(FLinearColor(1, 1, 1, 0.18f)))
			.SliderHandleColor(FSlateColor(Accent))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
		[
			SNew(SBox).MinDesiredWidth(64.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([Display]() { return Display(); })
				.Font(Font("Regular", 12))
				.ColorAndOpacity(FSlateColor(Muted))
				.Justification(ETextJustify::Right)
			]
		];
}

TSharedRef<SWidget> MakeToggle(TFunction<bool()> Get, TFunction<void(bool)> Set)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set](ECheckBoxState State) { Set(State == ECheckBoxState::Checked); })
			[
				SNew(SBox).Padding(FMargin(6, 2, 0, 2))
				[
					SNew(STextBlock)
					.Text_Lambda([Get]() { return Get() ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off"); })
					.Font(Font("Regular", 12))
					.ColorAndOpacity(FSlateColor(Muted))
				]
			]
		];
}

TSharedRef<SWidget> MakeChoice(TFunction<int32()> Get, TFunction<void(int32)> Set,
	TFunction<int32()> Count, TFunction<FText(int32)> Name)
{
	auto Step = [Get, Set, Count](int32 Delta)
	{
		const int32 Total = FMath::Max(Count(), 1);
		Set(((Get() + Delta) % Total + Total) % Total);
		return FReply::Handled();
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).ButtonStyle(&ButtonStyle())
			.OnClicked_Lambda([Step]() { return Step(-1); })
			[ Label(LOCTEXT("Prev", "<"), 12, Ink, "Bold") ]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(10, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([Get, Name]() { return Name(Get()); })
			.Font(Font("Regular", 12))
			.ColorAndOpacity(FSlateColor(Muted))
			.Justification(ETextJustify::Center)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).ButtonStyle(&ButtonStyle())
			.OnClicked_Lambda([Step]() { return Step(1); })
			[ Label(LOCTEXT("Next", ">"), 12, Ink, "Bold") ]
		];
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
void SUEGT2Menu::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SImage).Image(Box()).ColorAndOpacity(FSlateColor(Scrim))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(ContentHost, SBorder)
			.BorderImage(Box())
			.BorderBackgroundColor(FSlateColor(Panel))
			.Padding(FMargin(44.0f, 36.0f))
		]
	];

	Rebuild();
}

void SUEGT2Menu::SetMenuState(EUEGT2MenuState InState)
{
	MenuState = InState;
	Page = EUEGT2MenuPage::Root;
	PendingRebind.Reset();
	Rebuild();
}

void SUEGT2Menu::OpenSettings(int32 TabIndex)
{
	static const EUEGT2SettingsTab Tabs[] = {
		EUEGT2SettingsTab::Graphics, EUEGT2SettingsTab::Audio,
		EUEGT2SettingsTab::Controls, EUEGT2SettingsTab::Gameplay };
	Tab = Tabs[FMath::Clamp(TabIndex, 0, (int32)UE_ARRAY_COUNT(Tabs) - 1)];
	Page = EUEGT2MenuPage::Settings;
	PendingRebind.Reset();
	Rebuild();
}

void SUEGT2Menu::GoToPage(EUEGT2MenuPage InPage)
{
	Page = InPage;
	PendingRebind.Reset();
	Rebuild();
}

void SUEGT2Menu::OpenSurveyJournal()
{
	GoToPage(EUEGT2MenuPage::SurveyJournal);
}

TSharedRef<SWidget> SUEGT2Menu::BuildSurveyJournal()
{
	TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	return SNew(SBox).WidthOverride(720.0f)
	[
		SNew(SUEGT2SurveyJournal).Controller(WeakPC)
		.OnClose(FSimpleDelegate::CreateLambda([WeakPC]() { if (WeakPC.IsValid()) { WeakPC->CloseMenu(); } }))
	];
}

void SUEGT2Menu::SelectTab(EUEGT2SettingsTab InTab)
{
	Tab = InTab;
	PendingRebind.Reset();
	Rebuild();
}

void SUEGT2Menu::SelectDevTab(EUEGT2DevTab InDevTab)
{
	DevTab = InDevTab;
	PendingRebind.Reset();
	Rebuild();
}

void SUEGT2Menu::Rebuild()
{
	if (!ContentHost.IsValid())
	{
		return;
	}
	ContentHost->SetContent(
		Page == EUEGT2MenuPage::Root    ? BuildRoot() :
		Page == EUEGT2MenuPage::DevMode ? BuildDevMode() :
		Page == EUEGT2MenuPage::SurveyJournal ? BuildSurveyJournal() :
		                                  BuildSettings());
}

void SUEGT2Menu::ApplyAndSave(bool bResolutionToo)
{
	UUEGT2GameUserSettings* GameSettings = Settings();
	if (!GameSettings)
	{
		return;
	}
	if (bResolutionToo)
	{
		GameSettings->ApplyResolutionSettings(false);
	}
	GameSettings->ApplyNonResolutionSettings();
	GameSettings->SaveSettings();

	if (AUEGT2PlayerController* PC = Controller.Get())
	{
		PC->RebuildInputMappings();
	}
}

// ---------------------------------------------------------------------------
// Root page
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SUEGT2Menu::BuildRoot()
{
	const bool bMain = (MenuState == EUEGT2MenuState::Main);
	TSharedRef<SVerticalBox> Buttons = SNew(SVerticalBox);

	auto AddButton = [&Buttons](const FText& Text, TFunction<void()> Action)
	{
		Buttons->AddSlot().AutoHeight().Padding(0, 5)
		[
			SNew(SBox).WidthOverride(320.0f).HeightOverride(46.0f)
			[
				MenuButton(Text, FOnClicked::CreateLambda([Action]()
				{
					Action();
					return FReply::Handled();
				}))
			]
		];
	};

	TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	const bool bProgress = WeakPC.IsValid() && WeakPC->IsProgressEnabled();
	const bool bHasCheckpoint = bMain && bProgress && WeakPC->HasSavedProgress();

	if (bMain)
	{
		if (bHasCheckpoint)
		{
			AddButton(LOCTEXT("Continue", "Continue"), [WeakPC]()
				{ if (WeakPC.IsValid()) { WeakPC->ContinueProgress(); } });
		}
		AddButton(bProgress ? LOCTEXT("NewVisit", "New Visit") : LOCTEXT("Play", "Play"),
			[WeakPC]() { if (WeakPC.IsValid()) { WeakPC->StartPlaying(); } });
	}
	else
	{
		AddButton(LOCTEXT("Resume", "Resume"), [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->CloseMenu(); } });
		if (bProgress)
		{
			AddButton(LOCTEXT("SaveProgress", "Save Progress"), [WeakPC]()
				{ if (WeakPC.IsValid()) { WeakPC->SaveProgress(); } });
		}
		if (WeakPC.IsValid() && WeakPC->IsSurveyJournalEnabled())
		{
			AddButton(LOCTEXT("SurveyJournal", "Survey Journal"), [WeakPC]()
				{ if (WeakPC.IsValid()) { WeakPC->ToggleSurveyJournal(); } });
		}
	}

	AddButton(LOCTEXT("Settings", "Settings"), [this]() { GoToPage(EUEGT2MenuPage::Settings); });
	AddButton(LOCTEXT("DevMode", "Dev Mode"), [this]() { GoToPage(EUEGT2MenuPage::DevMode); });

	if (!bMain)
	{
		AddButton(LOCTEXT("ToMainMenu", "Main Menu"), [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->ReturnToMainMenu(); } });
	}

	AddButton(LOCTEXT("Quit", "Quit to Desktop"), [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->QuitGame(); } });

	TSharedRef<SWidget> Content = SNew(SBox).WidthOverride(430.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			Label(bMain ? LOCTEXT("Title", "FAIRHAVEN") : LOCTEXT("Paused", "PAUSED"), 44, Ink, "Bold")
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
		[
			Label(bMain
				? LOCTEXT("Subtitle", "a first-person open world  \x2022  v0.1")
				: LOCTEXT("PausedSub", "the world is waiting"),
				12, Muted)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 26, 0, 22)
		[
			SNew(SBox).HeightOverride(1.0f)
			[
				SNew(SImage).Image(Box()).ColorAndOpacity(FSlateColor(Divider))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			Buttons
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
		[
			SNew(STextBlock)
			.Visibility(bProgress ? EVisibility::Visible : EVisibility::Collapsed)
			.Text(bMain
				? LOCTEXT("ProgressMainHint", "Continue from your last checkpoint, or start a fresh visit. A new visit replaces it only when you save.")
				: LOCTEXT("ProgressPauseHint", "Save before leaving to keep this visit. Saving replaces your previous checkpoint."))
			.Font(Font("Regular", 11))
			.ColorAndOpacity(FSlateColor(Muted))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([WeakPC]() { return WeakPC.IsValid() ? WeakPC->GetProgressStatus() : FText::GetEmpty(); })
			.Font(Font("Regular", 11))
			.ColorAndOpacity(FSlateColor(Accent))
			.AutoWrapText(true)
		]
	];
	// Feature actions and a wrapped checkpoint error must remain reachable at
	// 720p. Leave room for the panel padding and a small viewport margin.
	return SNew(SBox)
		.MaxDesiredHeight_Lambda([this]()
		{
			const float Height = GetCachedGeometry().GetLocalSize().Y;
			return FOptionalSize(FMath::Max(240.0f, (Height > 0.0f ? Height : 720.0f) - 104.0f));
		})
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot() [ Content ]
		];
}

// ---------------------------------------------------------------------------
// Settings page
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SUEGT2Menu::BuildSettings()
{
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	auto AddTab = [this, &Tabs](const FText& Text, EUEGT2SettingsTab Which)
	{
		Tabs->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SButton)
			.ButtonStyle(&TabStyle(Tab == Which))
			.OnClicked_Lambda([this, Which]() { SelectTab(Which); return FReply::Handled(); })
			[ Label(Text, 13, Tab == Which ? Ink : Muted, "Bold") ]
		];
	};
	AddTab(LOCTEXT("TabGraphics", "Graphics"), EUEGT2SettingsTab::Graphics);
	AddTab(LOCTEXT("TabAudio", "Audio"), EUEGT2SettingsTab::Audio);
	AddTab(LOCTEXT("TabControls", "Controls"), EUEGT2SettingsTab::Controls);
	AddTab(LOCTEXT("TabGameplay", "Gameplay"), EUEGT2SettingsTab::Gameplay);

	TSharedRef<SWidget> Body =
		Tab == EUEGT2SettingsTab::Graphics ? BuildGraphicsTab() :
		Tab == EUEGT2SettingsTab::Audio    ? BuildAudioTab() :
		Tab == EUEGT2SettingsTab::Controls ? BuildControlsTab() :
		                                     BuildGameplayTab();

	return SNew(SBox).WidthOverride(720.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(LOCTEXT("SettingsTitle", "SETTINGS"), 30, Ink, "Bold")
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 14)
		[
			Tabs
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SBox).HeightOverride(420.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Body
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(150.0f).HeightOverride(40.0f)
				[
					MenuButton(LOCTEXT("Back", "Back"), FOnClicked::CreateLambda([this]()
					{
						GoToPage(EUEGT2MenuPage::Root);
						return FReply::Handled();
					}), 14)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(220.0f).HeightOverride(40.0f)
				[
					MenuButton(LOCTEXT("Recommended", "Recommended Defaults"), FOnClicked::CreateLambda([this]()
					{
						if (UUEGT2GameUserSettings* GameSettings = Settings())
						{
							GameSettings->ApplyRecommendedDefaults();
							ApplyAndSave(true);
							Rebuild();
						}
						return FReply::Handled();
					}), 12)
				]
			]
		]
	];
}

namespace
{
	/** Resolutions offered in the graphics tab. */
	const FIntPoint kResolutions[] = {
		{ 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }
	};
	const float kFrameLimits[] = { 0.0f, 30.0f, 60.0f, 120.0f, 144.0f, 240.0f };
}

TSharedRef<SWidget> SUEGT2Menu::BuildGraphicsTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	auto Add = [&List](TSharedRef<SWidget> Widget)
	{
		List->AddSlot().AutoHeight().Padding(0, 7)[ Widget ];
	};
	auto AddHeading = [&List](const FText& Text)
	{
		List->AddSlot().AutoHeight().Padding(0, 16, 0, 6)[ Label(Text, 12, Accent, "Bold") ];
	};

	UUEGT2GameUserSettings* S = Settings();
	if (!S)
	{
		return List;
	}

	AddHeading(LOCTEXT("Display", "DISPLAY"));

	Add(Row(LOCTEXT("WindowMode", "Window Mode"), MakeChoice(
		[S]() { return static_cast<int32>(S->GetFullscreenMode()); },
		[this, S](int32 Index)
		{
			S->SetFullscreenMode(static_cast<EWindowMode::Type>(Index));
			ApplyAndSave(true);
		},
		[]() { return 3; },
		[](int32 Index) -> FText
		{
			switch (Index)
			{
			case 0:  return LOCTEXT("Fullscreen", "Fullscreen");
			case 1:  return LOCTEXT("WindowedFullscreen", "Borderless");
			default: return LOCTEXT("Windowed", "Windowed");
			}
		})));

	Add(Row(LOCTEXT("Resolution", "Resolution"), MakeChoice(
		[S]()
		{
			const FIntPoint Current = S->GetScreenResolution();
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(kResolutions); ++Index)
			{
				if (kResolutions[Index] == Current) { return Index; }
			}
			return 2;
		},
		[this, S](int32 Index)
		{
			S->SetScreenResolution(kResolutions[FMath::Clamp(Index, 0, (int32)UE_ARRAY_COUNT(kResolutions) - 1)]);
			ApplyAndSave(true);
		},
		[]() { return (int32)UE_ARRAY_COUNT(kResolutions); },
		[](int32 Index)
		{
			const FIntPoint R = kResolutions[FMath::Clamp(Index, 0, (int32)UE_ARRAY_COUNT(kResolutions) - 1)];
			return FText::FromString(FString::Printf(TEXT("%d x %d"), R.X, R.Y));
		})));

	Add(Row(LOCTEXT("VSync", "V-Sync"), MakeToggle(
		[S]() { return S->IsVSyncEnabled(); },
		[this, S](bool bValue) { S->SetVSyncEnabled(bValue); ApplyAndSave(); })));

	Add(Row(LOCTEXT("FrameLimit", "Frame Rate Limit"), MakeChoice(
		[S]()
		{
			const float Current = S->GetFrameRateLimit();
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(kFrameLimits); ++Index)
			{
				if (FMath::IsNearlyEqual(kFrameLimits[Index], Current)) { return Index; }
			}
			return 0;
		},
		[this, S](int32 Index) { S->SetFrameRateLimit(kFrameLimits[FMath::Clamp(Index, 0, (int32)UE_ARRAY_COUNT(kFrameLimits) - 1)]); ApplyAndSave(); },
		[]() { return (int32)UE_ARRAY_COUNT(kFrameLimits); },
		[](int32 Index)
		{
			const float Limit = kFrameLimits[FMath::Clamp(Index, 0, (int32)UE_ARRAY_COUNT(kFrameLimits) - 1)];
			return Limit <= 0.0f ? LOCTEXT("Unlimited", "Unlimited")
				: FText::FromString(FString::Printf(TEXT("%.0f fps"), Limit));
		})));

	Add(Row(LOCTEXT("ResolutionScale", "Resolution Scale"), MakeSlider(
		[S]() { return S->GetResolutionScalePercent(); },
		[this, S](float Value) { S->SetResolutionScalePercent(Value); ApplyAndSave(); },
		50.0f, 100.0f,
		[S]() { return FText::FromString(FString::Printf(TEXT("%.0f%%"), S->GetResolutionScalePercent())); })));

	AddHeading(LOCTEXT("Quality", "QUALITY"));

	struct FQualityEntry
	{
		FText Name;
		TFunction<int32()> Get;
		TFunction<void(int32)> Set;
	};
	TArray<FQualityEntry> Quality;
	Quality.Add({ LOCTEXT("ViewDistance", "View Distance"), [S]() { return S->GetViewDistanceQuality(); }, [S](int32 V) { S->SetViewDistanceQuality(V); } });
	Quality.Add({ LOCTEXT("Shadows", "Shadows"), [S]() { return S->GetShadowQuality(); }, [S](int32 V) { S->SetShadowQuality(V); } });
	Quality.Add({ LOCTEXT("GlobalIllumination", "Global Illumination"), [S]() { return S->GetGlobalIlluminationQuality(); }, [S](int32 V) { S->SetGlobalIlluminationQuality(V); } });
	Quality.Add({ LOCTEXT("Reflections", "Reflections"), [S]() { return S->GetReflectionQuality(); }, [S](int32 V) { S->SetReflectionQuality(V); } });
	Quality.Add({ LOCTEXT("PostProcess", "Post Processing"), [S]() { return S->GetPostProcessingQuality(); }, [S](int32 V) { S->SetPostProcessingQuality(V); } });
	Quality.Add({ LOCTEXT("Textures", "Textures"), [S]() { return S->GetTextureQuality(); }, [S](int32 V) { S->SetTextureQuality(V); } });
	Quality.Add({ LOCTEXT("Effects", "Effects"), [S]() { return S->GetVisualEffectQuality(); }, [S](int32 V) { S->SetVisualEffectQuality(V); } });
	Quality.Add({ LOCTEXT("Foliage", "Foliage"), [S]() { return S->GetFoliageQuality(); }, [S](int32 V) { S->SetFoliageQuality(V); } });
	Quality.Add({ LOCTEXT("Shading", "Shading"), [S]() { return S->GetShadingQuality(); }, [S](int32 V) { S->SetShadingQuality(V); } });
	Quality.Add({ LOCTEXT("AntiAliasing", "Anti-Aliasing"), [S]() { return S->GetAntiAliasingQuality(); }, [S](int32 V) { S->SetAntiAliasingQuality(V); } });

	for (const FQualityEntry& Entry : Quality)
	{
		TFunction<int32()> Get = Entry.Get;
		TFunction<void(int32)> Set = Entry.Set;
		Add(Row(Entry.Name, MakeChoice(
			Get,
			[this, Set](int32 Index) { Set(Index); ApplyAndSave(); },
			[]() { return 4; },
			[](int32 Index) { return QualityName(Index); })));
	}

	Add(Row(LOCTEXT("FoliageDistance", "Foliage Draw Distance"), MakeChoice(
		[S]() { return S->GetFoliageDrawDistanceLevel(); },
		[this, S](int32 Index) { S->SetFoliageDrawDistanceLevel(Index); ApplyAndSave(); },
		[]() { return 4; },
		[](int32 Index) -> FText
		{
			switch (Index)
			{
			case 0:  return LOCTEXT("Near", "Near");
			case 1:  return LOCTEXT("Medium2", "Medium");
			case 2:  return LOCTEXT("Far", "Far");
			default: return LOCTEXT("VeryFar", "Very Far");
			}
		})));

	AddHeading(LOCTEXT("Image", "IMAGE"));

	Add(Row(LOCTEXT("FieldOfView", "Field of View"), MakeSlider(
		[S]() { return S->GetFieldOfView(); },
		[this, S](float Value) { S->SetFieldOfView(Value); ApplyAndSave(); },
		60.0f, 120.0f,
		[S]() { return FText::FromString(FString::Printf(TEXT("%.0f\x00B0"), S->GetFieldOfView())); })));

	Add(Row(LOCTEXT("Brightness", "Brightness"), MakeSlider(
		[S]() { return S->GetBrightness(); },
		[this, S](float Value) { S->SetBrightness(Value); ApplyAndSave(); },
		0.5f, 2.0f,
		[S]() { return FText::FromString(FString::Printf(TEXT("%.2f"), S->GetBrightness())); })));

	Add(Row(LOCTEXT("MotionBlur", "Motion Blur"), MakeToggle(
		[S]() { return S->GetMotionBlurEnabled(); },
		[this, S](bool bValue) { S->SetMotionBlurEnabled(bValue); ApplyAndSave(); })));

	Add(Row(LOCTEXT("Bloom", "Bloom"), MakeToggle(
		[S]() { return S->GetBloomEnabled(); },
		[this, S](bool bValue) { S->SetBloomEnabled(bValue); ApplyAndSave(); })));

	return List;
}

TSharedRef<SWidget> SUEGT2Menu::BuildAudioTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	UUEGT2GameUserSettings* S = Settings();
	if (!S)
	{
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("VolumeHeading", "VOLUME"), 12, Accent, "Bold") ];

	for (int32 Index = 0; Index < static_cast<int32>(EUEGT2AudioBus::Count); ++Index)
	{
		const EUEGT2AudioBus Bus = static_cast<EUEGT2AudioBus>(Index);
		List->AddSlot().AutoHeight().Padding(0, 8)
		[
			Row(UUEGT2GameUserSettings::GetAudioBusDisplayName(Bus), MakeSlider(
				[S, Bus]() { return S->GetAudioVolume(Bus); },
				[this, S, Bus](float Value) { S->SetAudioVolume(Bus, Value); ApplyAndSave(); },
				0.0f, 1.0f,
				[S, Bus]() { return FText::FromString(FString::Printf(TEXT("%.0f%%"), S->GetAudioVolume(Bus) * 100.0f)); }))
		];
	}
	return List;
}

TSharedRef<SWidget> SUEGT2Menu::BuildControlsTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	UUEGT2GameUserSettings* S = Settings();
	if (!S)
	{
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("LookHeading", "LOOK"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Sensitivity", "Mouse Sensitivity"), MakeSlider(
			[S]() { return S->GetMouseSensitivity(); },
			[this, S](float Value) { S->SetMouseSensitivity(Value); ApplyAndSave(); },
			0.1f, 4.0f,
			[S]() { return FText::FromString(FString::Printf(TEXT("%.2f"), S->GetMouseSensitivity())); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("InvertY", "Invert Look Y"), MakeToggle(
			[S]() { return S->GetInvertLookY(); },
			[this, S](bool bValue) { S->SetInvertLookY(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("ToggleSprint", "Toggle Sprint"), MakeToggle(
			[S]() { return S->GetToggleSprint(); },
			[this, S](bool bValue) { S->SetToggleSprint(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 18, 0, 6)
	[ Label(LOCTEXT("BindingsHeading", "KEY BINDINGS"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[ Label(LOCTEXT("BindingsHint", "Click a binding, then press a key. Escape cancels."), 11, Muted) ];

	for (const FUEGT2InputSlotDef& Def : UUEGT2InputConfig::GetSlotDefs())
	{
		const EUEGT2InputSlot Slot = Def.Slot;
		List->AddSlot().AutoHeight().Padding(0, 4)
		[
			Row(Def.DisplayName,
				SNew(SButton)
				.ButtonStyle(&ButtonStyle())
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this, Slot]()
				{
					PendingRebind = Slot;
					FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
					Rebuild();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this, Slot]()
					{
						if (PendingRebind.IsSet() && PendingRebind.GetValue() == Slot)
						{
							return LOCTEXT("PressAKey", "press a key...");
						}
						const FKey Key = UUEGT2InputConfig::GetEffectiveKey(Slot);
						return Key.IsValid() ? Key.GetDisplayName() : LOCTEXT("Unbound", "unbound");
					})
					.Font(Font("Regular", 12))
					.ColorAndOpacity(FSlateColor(Muted))
				])
		];
	}

	List->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		SNew(SBox).WidthOverride(220.0f).HeightOverride(38.0f).HAlign(HAlign_Left)
		[
			MenuButton(LOCTEXT("ResetBindings", "Reset Bindings"), FOnClicked::CreateLambda([this]()
			{
				if (UUEGT2GameUserSettings* GameSettings = Settings())
				{
					GameSettings->ClearKeyOverrides();
					ApplyAndSave();
					Rebuild();
				}
				return FReply::Handled();
			}), 12)
		]
	];

	return List;
}

TSharedRef<SWidget> SUEGT2Menu::BuildGameplayTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	UUEGT2GameUserSettings* S = Settings();
	if (!S)
	{
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("ProgressHeading", "PROGRESS"), 12, Accent, "Bold") ];
	TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		SNew(SBox)
		.IsEnabled_Lambda([WeakPC]() { return WeakPC.IsValid() && WeakPC->IsProgressAvailable(); })
		[
			Row(LOCTEXT("SaveProgressSetting", "Save Progress"), MakeToggle(
				[S]() { return S->GetSaveProgressEnabled(); },
				[this, S](bool bValue) { S->SetSaveProgressEnabled(bValue); ApplyAndSave(); }))
		]
	];
	List->AddSlot().AutoHeight().Padding(0, 2, 0, 16)
	[
		SNew(STextBlock)
		.Text_Lambda([WeakPC]()
		{
			return WeakPC.IsValid() && WeakPC->IsProgressAvailable()
				? LOCTEXT("SaveProgressSettingHint", "Enable checkpoints and Continue. Turning this off keeps your existing save.")
				: LOCTEXT("SaveProgressUnavailable", "Progress saving is disabled for this session.");
		})
		.Font(Font("Regular", 11))
		.ColorAndOpacity(FSlateColor(Muted))
		.AutoWrapText(true)
	];

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("CameraHeading", "CAMERA & HUD"), 12, Accent, "Bold") ];
	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		SNew(SBox)
		.IsEnabled_Lambda([WeakPC]() { return WeakPC.IsValid() && WeakPC->IsSurveyJournalAvailable(); })
		[
			Row(LOCTEXT("SurveyJournalSetting", "Survey Journal"), MakeToggle(
				[S]() { return S->GetSurveyJournalEnabled(); },
				[this, S](bool bValue) { S->SetSurveyJournalEnabled(bValue); ApplyAndSave(); }))
		]
	];
	List->AddSlot().AutoHeight().Padding(0, 2, 0, 12)
	[
		SNew(STextBlock)
		.Text_Lambda([WeakPC]()
		{
			return WeakPC.IsValid() && WeakPC->IsSurveyJournalAvailable()
				? LOCTEXT("SurveyJournalSettingHint", "Review surveyed places and track directions back to them. Turning this off keeps your discoveries.")
				: LOCTEXT("SurveyJournalUnavailable", "The survey journal is disabled for this session.");
		})
		.Font(Font("Regular", 11)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true)
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("HeadBob", "Head Bob"), MakeSlider(
			[S]() { return S->GetHeadBobScale(); },
			[this, S](float Value) { S->SetHeadBobScale(Value); ApplyAndSave(); },
			0.0f, 2.0f,
			[S]() { return FText::FromString(FString::Printf(TEXT("%.2f"), S->GetHeadBobScale())); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Crosshair", "Show Crosshair"), MakeToggle(
			[S]() { return S->GetShowCrosshair(); },
			[this, S](bool bValue) { S->SetShowCrosshair(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Prompts", "Show Interact Prompts"), MakeToggle(
			[S]() { return S->GetShowInteractPrompts(); },
			[this, S](bool bValue) { S->SetShowInteractPrompts(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 16, 0, 10)
	[ Label(LOCTEXT("TownHeading", "THE TOWN"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Bubbles", "Speech Bubbles"), MakeToggle(
			[S]() { return S->GetShowSpeechBubbles(); },
			[this, S](bool bValue) { S->SetShowSpeechBubbles(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("BubblesHint", "What the townsfolk are about to go and do."), 11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Needs", "Needs and Purse"), MakeToggle(
			[S]() { return S->GetShowNeeds(); },
			[this, S](bool bValue) { S->SetShowNeeds(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("NeedsHint",
		"How you are keeping, and what is in your pocket."), 11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Almanac", "Date and Weather"), MakeToggle(
			[S]() { return S->GetShowAlmanac(); },
			[this, S](bool bValue) { S->SetShowAlmanac(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("AlmanacHint",
		"The clock, the date and the temperature, top left."), 11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Fahrenheit", "Fahrenheit"), MakeToggle(
			[S]() { return S->GetUseFahrenheit(); },
			[this, S](bool bValue) { S->SetUseFahrenheit(bValue); ApplyAndSave(); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("Crowd", "Crowd Density"), MakeSlider(
			[S]() { return S->GetCrowdDensity(); },
			[this, S](float Value) { S->SetCrowdDensity(Value); ApplyAndSave(); },
			0.1f, 1.0f,
			[S]() { return FText::FromString(FString::Printf(TEXT("%.0f%%"), S->GetCrowdDensity() * 100.0f)); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("CrowdHint", "How many inhabitants are present. Turn it down if the town costs you frames."), 11, Muted) ];

	return List;
}

// ---------------------------------------------------------------------------
// Dev mode page
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SUEGT2Menu::BuildDevMode()
{
	TSharedRef<SHorizontalBox> DevTabs = SNew(SHorizontalBox);
	auto AddDevTab = [this, &DevTabs](const FText& Text, EUEGT2DevTab Which)
	{
		DevTabs->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SButton)
			.ButtonStyle(&TabStyle(DevTab == Which))
			.OnClicked_Lambda([this, Which]() { SelectDevTab(Which); return FReply::Handled(); })
			[ Label(Text, 13, DevTab == Which ? Ink : Muted, "Bold") ]
		];
	};
	AddDevTab(LOCTEXT("DevTabPlayer", "Player"), EUEGT2DevTab::Player);
	AddDevTab(LOCTEXT("DevTabWorld", "World"), EUEGT2DevTab::World);
	AddDevTab(LOCTEXT("DevTabLife", "Life"), EUEGT2DevTab::Life);
	AddDevTab(LOCTEXT("DevTabDisplay", "Display"), EUEGT2DevTab::Display);
	AddDevTab(LOCTEXT("DevTabTeleport", "Teleport"), EUEGT2DevTab::Teleport);

	TSharedRef<SWidget> Body =
		DevTab == EUEGT2DevTab::Player  ? BuildDevPlayerTab() :
		DevTab == EUEGT2DevTab::World   ? BuildDevWorldTab() :
		DevTab == EUEGT2DevTab::Life    ? BuildDevLifeTab() :
		DevTab == EUEGT2DevTab::Display ? BuildDevDisplayTab() :
		                                  BuildDevTeleportTab();

	return SNew(SBox).WidthOverride(720.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(LOCTEXT("DevTitle", "DEV MODE"), 30, Ink, "Bold")
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
		[
			Label(LOCTEXT("DevSubtitle", "free camera, world controls and diagnostics"), 12, Muted)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 14)
		[
			DevTabs
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SBox).HeightOverride(420.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Body
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 18, 0, 0)
		[
			SNew(SBox).WidthOverride(150.0f).HeightOverride(40.0f)
			[
				MenuButton(LOCTEXT("Back", "Back"), FOnClicked::CreateLambda([this]()
				{
					GoToPage(EUEGT2MenuPage::Root);
					return FReply::Handled();
				}), 14)
			]
		]
	];
}

TSharedRef<SWidget> SUEGT2Menu::BuildDevPlayerTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	// Resolved on every read rather than captured once: the subsystem dies with
	// the world, and this menu outlives a quit to the front end.
	auto D = [WeakPC]() { return DevOf(WeakPC); };

	if (!D())
	{
		List->AddSlot().AutoHeight()
		[ Label(LOCTEXT("DevNoWorld", "No world loaded."), 13, Muted) ];
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("DevHeadingMaster", "DEV MODE"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevEnabled", "Enable Dev Mode"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsDevModeEnabled(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetDevModeEnabled(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 12)
	[ Label(LOCTEXT("DevEnabledHint", "Turning it off restores walking, collision, jumps and speed."), 11, Muted) ];

	if (!D()->HasPlayer())
	{
		List->AddSlot().AutoHeight().Padding(0, 10)
		[ Label(LOCTEXT("DevNoPawn", "Start playing to use the player controls."), 12, Muted) ];
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 10)
	[ Label(LOCTEXT("DevHeadingMovement", "MOVEMENT"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevGod", "God Mode"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsGodMode(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetGodMode(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevFly", "Fly"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsFlyEnabled(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetFlyEnabled(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevNoclip", "Noclip"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsNoclipEnabled(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetNoclipEnabled(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("DevNoclipHint", "Noclip turns flight on; clearing flight clears noclip."), 11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevSpeed", "Speed"), MakeSlider(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? Dev->GetSpeedMultiplier() : 1.0f; },
			[D](float Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetSpeedMultiplier(Value); } },
			1.0f, 50.0f,
			[D]()
			{
				UUEGT2DevModeSubsystem* Dev = D();
				return FText::FromString(FString::Printf(TEXT("%.1fx"), Dev ? Dev->GetSpeedMultiplier() : 1.0f));
			}))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("DevFlyHint", "Flying: W A S D follow the camera, Space up, Ctrl down, Shift faster."), 11, Muted) ];

	return List;
}

TSharedRef<SWidget> SUEGT2Menu::BuildDevWorldTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	auto D = [WeakPC]() { return DevOf(WeakPC); };

	if (!D())
	{
		List->AddSlot().AutoHeight()
		[ Label(LOCTEXT("DevNoWorldW", "No world loaded."), 13, Muted) ];
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("DevHeadingTime", "TIME OF DAY"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevTime", "Time of Day"), MakeSlider(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? Dev->GetTimeOfDay() : 12.0f; },
			[D](float Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetTimeOfDay(Value); } },
			0.0f, 24.0f,
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return HourText(Dev ? Dev->GetTimeOfDay() : 12.0f); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevCycle", "Day/Night Cycle"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsDayNightCycleEnabled(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetDayNightCycleEnabled(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevDayLength", "Day Length"), MakeSlider(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? Dev->GetDayLengthMinutes() : 20.0f; },
			[D](float Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetDayLengthMinutes(Value); } },
			1.0f, 120.0f,
			[D]()
			{
				UUEGT2DevModeSubsystem* Dev = D();
				return FText::FromString(FString::Printf(TEXT("%.0f min"), Dev ? Dev->GetDayLengthMinutes() : 20.0f));
			}))
	];

	List->AddSlot().AutoHeight().Padding(0, 16, 0, 10)
	[ Label(LOCTEXT("DevHeadingWeather", "WEATHER"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevWeather", "Weather"), MakeChoice(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? (int32)Dev->GetWeather() : 0; },
			[D](int32 Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetWeather((EUEGT2Weather)Value); } },
			[]() { return (int32)EUEGT2Weather::Count; },
			[](int32 Value) { return GetWeatherDisplayName((EUEGT2Weather)Value); }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevFog", "Fog Density"), MakeSlider(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? Dev->GetFogDensity() : 0.012f; },
			[D](float Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetFogDensity(Value); } },
			0.0f, 0.25f,
			[D]()
			{
				UUEGT2DevModeSubsystem* Dev = D();
				return FText::FromString(FString::Printf(TEXT("%.3f"), Dev ? Dev->GetFogDensity() : 0.0f));
			}))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("DevWeatherHint", "Presets change light, fog and cloud height. There is no precipitation."), 11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 16, 0, 10)
	[ Label(LOCTEXT("DevHeadingTimeScale", "TIME SCALE"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevGameSpeed", "Game Speed"), MakeSlider(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? Dev->GetGameSpeed() : 1.0f; },
			[D](float Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetGameSpeed(Value); } },
			0.1f, 5.0f,
			[D]()
			{
				UUEGT2DevModeSubsystem* Dev = D();
				return FText::FromString(FString::Printf(TEXT("%.2fx"), Dev ? Dev->GetGameSpeed() : 1.0f));
			}))
	];

	return List;
}

TSharedRef<SWidget> SUEGT2Menu::BuildDevLifeTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	auto D = [WeakPC]() { return DevOf(WeakPC); };

	if (!D())
	{
		List->AddSlot().AutoHeight()
		[ Label(LOCTEXT("DevNoWorldL", "No world loaded."), 13, Muted) ];
		return List;
	}

	// The player's own day comes first, and above the population check: the
	// player has needs and a purse whether or not the npc stage has been run.
	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("DevHeadingYou", "YOUR DAY"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[
		SNew(STextBlock)
		.Text_Lambda([D]()
		{
			UUEGT2DevModeSubsystem* Dev = D();
			return Dev ? Dev->GetPlayerLifeSummary() : FText::GetEmpty();
		})
		.Font(Font("Regular", 12))
		.ColorAndOpacity(FSlateColor(Muted))
	];

	List->AddSlot().AutoHeight().Padding(0, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SBox).WidthOverride(150.0f).HeightOverride(36.0f)
			[
				MenuButton(LOCTEXT("DevGiveCoin", "+50 Coins"), FOnClicked::CreateLambda([D]()
				{
					if (UUEGT2DevModeSubsystem* Dev = D())
					{
						Dev->SetPlayerCoins(Dev->GetPlayerCoins() + 50);
					}
					return FReply::Handled();
				}), 12)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SBox).WidthOverride(150.0f).HeightOverride(36.0f)
			[
				MenuButton(LOCTEXT("DevBroke", "Empty Purse"), FOnClicked::CreateLambda([D]()
				{
					if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetPlayerCoins(0); }
					return FReply::Handled();
				}), 12)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SBox).WidthOverride(150.0f).HeightOverride(36.0f)
			[
				MenuButton(LOCTEXT("DevFillNeeds", "Fill Needs"), FOnClicked::CreateLambda([D]()
				{
					if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetPlayerNeedsSatisfied(true); }
					return FReply::Handled();
				}), 12)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(150.0f).HeightOverride(36.0f)
			[
				MenuButton(LOCTEXT("DevDrainNeeds", "Empty Needs"), FOnClicked::CreateLambda([D]()
				{
					if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetPlayerNeedsSatisfied(false); }
					return FReply::Handled();
				}), 12)
			]
		]
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("DevYourDayHint",
		"A need takes world hours to run down, and a day is twenty minutes. "
		"These are for getting to the interesting state without living through it."),
		11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 10)
	[ Label(LOCTEXT("DevHeadingPopulation", "POPULATION"), 12, Accent, "Bold") ];

	if (!D()->HasPopulation())
	{
		List->AddSlot().AutoHeight().Padding(0, 6)
		[ Label(LOCTEXT("DevNoPopulation",
			"Nobody is home. Run Build-Content.ps1 -Stages npc to populate the map."),
			12, Muted) ];
		return List;
	}

	// Live text rather than a snapshot: the counts move while the menu is open,
	// which is the quickest way to see the schedule actually doing something.
	List->AddSlot().AutoHeight().Padding(0, 4)
	[
		SNew(STextBlock)
		.Text_Lambda([D]()
		{
			UUEGT2DevModeSubsystem* Dev = D();
			if (!Dev) { return FText::GetEmpty(); }
			return FText::FromString(FString::Printf(TEXT("%d people    %d animals    %d out    %d talking"),
				Dev->GetPeopleCount(), Dev->GetAnimalCount(),
				Dev->GetActiveNPCCount(), Dev->GetSpeakingCount()));
		})
		.Font(Font("Regular", 13))
		.ColorAndOpacity(FSlateColor(Ink))
	];

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[
		SNew(STextBlock)
		.Text_Lambda([D]()
		{
			UUEGT2DevModeSubsystem* Dev = D();
			if (!Dev) { return FText::GetEmpty(); }
			const FString DayLabel = Dev->GetDayLabel().ToString();
			return FText::FromString(FString::Printf(TEXT("day %d%s    %s"),
				Dev->GetDayIndex() + 1,
				DayLabel.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  -  %s"), *DayLabel),
				*HourText(Dev->GetTimeOfDay()).ToString()));
		})
		.Font(Font("Regular", 12))
		.ColorAndOpacity(FSlateColor(Muted))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevCrowd", "Crowd Density"), MakeSlider(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? Dev->GetCrowdDensity() : 1.0f; },
			[D](float Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetCrowdDensity(Value); } },
			0.1f, 1.0f,
			[D]()
			{
				UUEGT2DevModeSubsystem* Dev = D();
				return FText::FromString(FString::Printf(TEXT("%.0f%%"),
					(Dev ? Dev->GetCrowdDensity() : 1.0f) * 100.0f));
			}))
	];

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 10)
	[ Label(LOCTEXT("DevHeadingBehaviour", "BEHAVIOUR"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevFreeze", "Freeze Schedules"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->AreSchedulesPaused(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetSchedulesPaused(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("DevFreezeHint",
		"Stops everyone re-deciding. They finish walking to wherever they were going."),
		11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevNPCDebug", "Show Plans"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsNPCDebugVisible(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetNPCDebugVisible(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("DevNPCDebugHint",
		"Draws each nearby inhabitant's activity and destination. The label colour is why: "
		"grey routine, blue weather, amber you, red a need, green the day, violet a detour."),
		11, Muted) ];

	List->AddSlot().AutoHeight().Padding(0, 12)
	[
		SNew(SBox).WidthOverride(200.0f).HeightOverride(38.0f).HAlign(HAlign_Left)
		[
			MenuButton(LOCTEXT("DevChatter", "Everyone Speak"), FOnClicked::CreateLambda([D]()
			{
				if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->TriggerChatter(); }
				return FReply::Handled();
			}), 12)
		]
	];

	List->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[ Label(LOCTEXT("DevChatterHint",
		"Every inhabitant within earshot announces what they are about to do."), 11, Muted) ];

	return List;
}

TSharedRef<SWidget> SUEGT2Menu::BuildDevDisplayTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	auto D = [WeakPC]() { return DevOf(WeakPC); };

	if (!D())
	{
		List->AddSlot().AutoHeight()
		[ Label(LOCTEXT("DevNoWorldD", "No world loaded."), 13, Muted) ];
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("DevHeadingView", "VIEW"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevViewMode", "View Mode"), MakeChoice(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev ? (int32)Dev->GetViewMode() : 0; },
			[D](int32 Value) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetViewMode((EUEGT2ViewMode)Value); } },
			[]() { return (int32)EUEGT2ViewMode::Count; },
			[](int32 Value)
			{
				switch ((EUEGT2ViewMode)Value)
				{
				case EUEGT2ViewMode::Unlit:     return LOCTEXT("ViewUnlit", "Unlit");
				case EUEGT2ViewMode::Wireframe: return LOCTEXT("ViewWireframe", "Wireframe");
				default:                        return LOCTEXT("ViewLit", "Lit");
				}
			}))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevCollision", "Show Collision"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsShowCollision(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetShowCollision(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 16, 0, 10)
	[ Label(LOCTEXT("DevHeadingOverlays", "OVERLAYS"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevDiagnostics", "Diagnostics Overlay"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsDiagnosticsVisible(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetDiagnosticsVisible(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevStatFps", "stat fps"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsStatFps(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetStatFps(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevStatUnit", "stat unit"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsStatUnit(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetStatUnit(bValue); } }))
	];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		Row(LOCTEXT("DevProbe", "Draw Interaction Probe"), MakeToggle(
			[D]() { UUEGT2DevModeSubsystem* Dev = D(); return Dev && Dev->IsDrawInteractionProbe(); },
			[D](bool bValue) { if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SetDrawInteractionProbe(bValue); } }))
	];

	return List;
}

TSharedRef<SWidget> SUEGT2Menu::BuildDevTeleportTab()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TWeakObjectPtr<AUEGT2PlayerController> WeakPC = Controller;
	auto D = [WeakPC]() { return DevOf(WeakPC); };

	if (!D())
	{
		List->AddSlot().AutoHeight()
		[ Label(LOCTEXT("DevNoWorldT", "No world loaded."), 13, Muted) ];
		return List;
	}

	List->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[ Label(LOCTEXT("DevHeadingPosition", "POSITION"), 12, Accent, "Bold") ];

	List->AddSlot().AutoHeight().Padding(0, 7)
	[
		SNew(STextBlock)
		.Text_Lambda([D]()
		{
			UUEGT2DevModeSubsystem* Dev = D();
			if (!Dev || !Dev->HasPlayer())
			{
				return LOCTEXT("DevNoPawnShort", "No pawn.");
			}
			const FVector Location = Dev->GetPlayerLocation();
			return FText::FromString(FString::Printf(TEXT("X %.0f    Y %.0f    Z %.0f"),
				Location.X, Location.Y, Location.Z));
		})
		.Font(Font("Regular", 13))
		.ColorAndOpacity(FSlateColor(Ink))
	];

	List->AddSlot().AutoHeight().Padding(0, 12)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
		[
			SNew(SBox).WidthOverride(160.0f).HeightOverride(38.0f)
			[
				MenuButton(LOCTEXT("DevSavePos", "Save Position"), FOnClicked::CreateLambda([D]()
				{
					if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->SavePosition(); }
					return FReply::Handled();
				}), 12)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(180.0f).HeightOverride(38.0f)
			[
				MenuButton(LOCTEXT("DevRestorePos", "Restore Position"), FOnClicked::CreateLambda([D]()
				{
					if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->RestorePosition(); }
					return FReply::Handled();
				}), 12)
			]
		]
	];

	List->AddSlot().AutoHeight().Padding(0, 16, 0, 10)
	[ Label(LOCTEXT("DevHeadingViewpoints", "VIEWPOINTS"), 12, Accent, "Bold") ];

	// Two columns, over the same list the screenshot tour walks.
	const TArray<FUEGT2Viewpoint>& Tour = UUEGT2CaptureSubsystem::GetTour();
	for (int32 Index = 0; Index < Tour.Num(); Index += 2)
	{
		TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);
		for (int32 Column = 0; Column < 2; ++Column)
		{
			const int32 Which = Index + Column;
			if (!Tour.IsValidIndex(Which))
			{
				RowBox->AddSlot().FillWidth(0.5f)[ SNew(SSpacer) ];
				continue;
			}
			const FText Name = FText::FromName(Tour[Which].Name);
			RowBox->AddSlot().FillWidth(0.5f).Padding(0, 0, 8, 0)
			[
				SNew(SBox).HeightOverride(36.0f)
				[
					MenuButton(Name, FOnClicked::CreateLambda([D, Which]()
					{
						if (UUEGT2DevModeSubsystem* Dev = D()) { Dev->TeleportToViewpoint(Which); }
						return FReply::Handled();
					}), 12)
				]
			];
		}
		List->AddSlot().AutoHeight().Padding(0, 4)[ RowBox ];
	}

	return List;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
FReply SUEGT2Menu::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (PendingRebind.IsSet())
	{
		const FKey Key = KeyEvent.GetKey();
		const EUEGT2InputSlot Slot = PendingRebind.GetValue();
		PendingRebind.Reset();

		if (Key != EKeys::Escape)
		{
			if (UUEGT2GameUserSettings* GameSettings = Settings())
			{
				if (const FUEGT2InputSlotDef* Def = UUEGT2InputConfig::FindSlot(Slot))
				{
					GameSettings->SetKeyOverride(Def->Name, Key);
					ApplyAndSave();
				}
			}
		}
		Rebuild();
		return FReply::Handled();
	}

	const FKey Key = KeyEvent.GetKey();
	if (Page == EUEGT2MenuPage::SurveyJournal && (Key == EKeys::Escape
		|| Key == UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Journal)
		|| Key == UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Menu)
		|| Key == EKeys::Gamepad_Special_Left || Key == EKeys::Gamepad_Special_Right))
	{
		// UIOnly routes the paused shortcut through Slate, not Enhanced Input.
		if (AUEGT2PlayerController* PC = Controller.Get()) { PC->CloseMenu(); }
		return FReply::Handled();
	}
	if (Page == EUEGT2MenuPage::Root && MenuState == EUEGT2MenuState::Pause
		&& (Key == UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Journal) || Key == EKeys::Gamepad_Special_Left))
	{
		if (AUEGT2PlayerController* PC = Controller.Get(); PC && PC->IsSurveyJournalEnabled())
		{
			PC->ToggleSurveyJournal();
			return FReply::Handled();
		}
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		if (Page == EUEGT2MenuPage::Settings || Page == EUEGT2MenuPage::DevMode)
		{
			GoToPage(EUEGT2MenuPage::Root);
			return FReply::Handled();
		}
		if (MenuState == EUEGT2MenuState::Pause)
		{
			if (AUEGT2PlayerController* PC = Controller.Get())
			{
				PC->CloseMenu();
			}
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SUEGT2Menu::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (PendingRebind.IsSet()) { return FReply::Unhandled(); }
	// Enter/Space can be rebound to Journal. Its shortcut must take priority
	// over Slate's Accept action on both Track and the pause root's buttons.
	if (Page == EUEGT2MenuPage::SurveyJournal) { return OnKeyDown(Geometry, KeyEvent); }
	const AUEGT2PlayerController* PC = Controller.Get();
	const FKey Key = KeyEvent.GetKey();
	if (Page == EUEGT2MenuPage::Root && MenuState == EUEGT2MenuState::Pause
		&& PC && PC->IsSurveyJournalEnabled()
		&& (Key == UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Journal) || Key == EKeys::Gamepad_Special_Left))
	{
		return OnKeyDown(Geometry, KeyEvent);
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
