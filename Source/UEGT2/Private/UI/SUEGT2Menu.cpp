#include "SUEGT2Menu.h"

#include "Framework/Application/SlateApplication.h"
#include "Player/UEGT2InputConfig.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Styling/CoreStyle.h"
#include "UEGT2LogChannels.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
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
	static const FLinearColor Ink(0.94f, 0.95f, 0.97f, 1.0f);
	static const FLinearColor Muted(0.66f, 0.69f, 0.74f, 1.0f);
	static const FLinearColor Accent(0.42f, 0.78f, 0.80f, 1.0f);
	static const FLinearColor Scrim(0.02f, 0.03f, 0.05f, 0.52f);
	static const FLinearColor Panel(0.05f, 0.07f, 0.09f, 0.88f);
	static const FLinearColor Divider(1.0f, 1.0f, 1.0f, 0.08f);

	static const FSlateBrush* Box()
	{
		return FCoreStyle::Get().GetBrush("GenericWhiteBox");
	}

	static FSlateFontInfo Font(const ANSICHAR* Style, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

	static FSlateBrush Tinted(const FLinearColor& Colour)
	{
		FSlateBrush Brush = *Box();
		Brush.TintColor = FSlateColor(Colour);
		return Brush;
	}

	static const FButtonStyle& ButtonStyle()
	{
		static const FButtonStyle Style = []
		{
			FButtonStyle Result;
			Result.SetNormal(Tinted(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f)));
			Result.SetHovered(Tinted(FLinearColor(Accent.R, Accent.G, Accent.B, 0.28f)));
			Result.SetPressed(Tinted(FLinearColor(Accent.R, Accent.G, Accent.B, 0.46f)));
			Result.SetNormalPadding(FMargin(16.0f, 9.0f));
			Result.SetPressedPadding(FMargin(16.0f, 9.0f));
			return Result;
		}();
		return Style;
	}

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

	static TSharedRef<SWidget> Label(const FText& Text, int32 Size = 13,
		const FLinearColor& Colour = Ink, const ANSICHAR* Style = "Regular")
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(Font(Style, Size))
			.ColorAndOpacity(FSlateColor(Colour));
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

void SUEGT2Menu::SelectTab(EUEGT2SettingsTab InTab)
{
	Tab = InTab;
	PendingRebind.Reset();
	Rebuild();
}

void SUEGT2Menu::Rebuild()
{
	if (!ContentHost.IsValid())
	{
		return;
	}
	ContentHost->SetContent(Page == EUEGT2MenuPage::Root ? BuildRoot() : BuildSettings());
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

	if (bMain)
	{
		AddButton(LOCTEXT("Play", "Play"), [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->StartPlaying(); } });
	}
	else
	{
		AddButton(LOCTEXT("Resume", "Resume"), [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->CloseMenu(); } });
	}

	AddButton(LOCTEXT("Settings", "Settings"), [this]() { GoToPage(EUEGT2MenuPage::Settings); });

	if (!bMain)
	{
		AddButton(LOCTEXT("ToMainMenu", "Main Menu"), [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->ReturnToMainMenu(); } });
	}

	AddButton(LOCTEXT("Quit", "Quit to Desktop"), [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->QuitGame(); } });

	return SNew(SBox).WidthOverride(430.0f)
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
	[ Label(LOCTEXT("CameraHeading", "CAMERA & HUD"), 12, Accent, "Bold") ];

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

	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		if (Page == EUEGT2MenuPage::Settings)
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

#undef LOCTEXT_NAMESPACE
