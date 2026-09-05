#include "UI/SUEGT2SurveyJournal.h"

#include "Player/UEGT2PlayerController.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "UI/UEGT2UIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEGT2SurveyJournal"

void SUEGT2SurveyJournal::Construct(const FArguments& InArgs)
{
	using namespace UEGT2UI;
	const AUEGT2PlayerController* Controller = InArgs._Controller.Get();
	Survey = UUEGT2SurveySubsystem::Get(Controller ? Controller->GetWorld() : nullptr);
	OnClose = InArgs._OnClose;

	// The world is paused while this page is open. Read the roster once rather
	// than scanning landmark actors or rebuilding rows during Slate's tick.
	const TArray<FUEGT2SurveyEntry> Entries = IsJournalEnabled()
		? Survey->GetEntries() : TArray<FUEGT2SurveyEntry>();
	int32 Discovered = 0;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FUEGT2SurveyEntry& Entry : Entries)
	{
		Discovered += Entry.bDiscovered ? 1 : 0;
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			BuildRow(Entry)
		];
	}
	if (Entries.IsEmpty())
	{
		Rows->AddSlot().AutoHeight().Padding(12.0f)
		[
			Label(LOCTEXT("NoLandmarks", "There are no survey landmarks in this world."), 13, Muted)
		];
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(LOCTEXT("Title", "Survey Journal"), 30, Ink, "Bold")
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility_Lambda([this] { return IsJournalEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text(FText::Format(LOCTEXT("SurveyCount", "{0} of {1} surveyed"),
				FText::AsNumber(Discovered), FText::AsNumber(Entries.Num())))
			.Font(Font("Bold", 14))
			.ColorAndOpacity(FSlateColor(Accent))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 16.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]
			{
				return IsJournalEnabled()
					? LOCTEXT("DirectionsHint", "Track a surveyed place to see its compass direction and straight-line distance while you walk.")
					: LOCTEXT("Disabled", "The Survey Journal is turned off or unavailable in this session. Your discoveries are unchanged.");
			})
			.Font(Font("Regular", 12))
			.ColorAndOpacity(FSlateColor(Muted))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(330.0f)
			.Visibility_Lambda([this] { return IsJournalEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Rows
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(170.0f).HeightOverride(40.0f)
				[
					SNew(SButton)
					.ButtonStyle(&ButtonStyle())
					.HAlign(HAlign_Center)
					.OnClicked_Lambda([this]
					{
						OnClose.ExecuteIfBound();
						return FReply::Handled();
					})
					[ Label(LOCTEXT("Close", "Close Journal"), 13) ]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(170.0f).HeightOverride(40.0f)
				.Visibility_Lambda([this] { return IsJournalEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SButton)
					.ButtonStyle(&ButtonStyle())
					.HAlign(HAlign_Center)
					.IsEnabled_Lambda([this] { return IsJournalEnabled() && !GetTrackedId().IsNone(); })
					.OnClicked_Lambda([this]
					{
						if (IsJournalEnabled()) { Survey->ClearTracking(); }
						return FReply::Handled();
					})
					[ Label(LOCTEXT("StopTracking", "Stop Tracking"), 13) ]
				]
			]
		]
	];
}

TSharedRef<SWidget> SUEGT2SurveyJournal::BuildRow(const FUEGT2SurveyEntry& Entry)
{
	using namespace UEGT2UI;
	const FName Id = Entry.Id;
	TSharedRef<SWidget> TrackingControl = SNew(SSpacer).Size(FVector2D(112.0f, 36.0f));
	if (Entry.bDiscovered)
	{
		TrackingControl = SNew(SBox).WidthOverride(112.0f).HeightOverride(36.0f)
		[
			SNew(SButton)
			.ButtonStyle(&ButtonStyle())
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this, Id] { return IsJournalEnabled() && GetTrackedId() != Id; })
			.OnClicked_Lambda([this, Id]
			{
				if (IsJournalEnabled()) { Survey->TrackLandmark(Id); }
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Id] { return GetTrackedId() == Id ? LOCTEXT("Tracking", "Tracking") : LOCTEXT("Track", "Track"); })
				.Font(Font("Bold", 12))
				.ColorAndOpacity(FSlateColor(Ink))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(Box())
		.BorderBackgroundColor(FSlateColor(Well))
		.Padding(FMargin(12.0f, 8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Entry.Name)
					.Font(Font("Bold", 14))
					.ColorAndOpacity(FSlateColor(Ink))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[
					Label(Entry.bDiscovered ? LOCTEXT("Surveyed", "Surveyed") : LOCTEXT("Unsurveyed", "Not yet surveyed"),
					11, Entry.bDiscovered ? Accent : Muted)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				TrackingControl
			]
		];
}

bool SUEGT2SurveyJournal::IsJournalEnabled() const
{
	const UUEGT2SurveySubsystem* Service = Survey.Get();
	return Service && Service->IsEnabled();
}

FName SUEGT2SurveyJournal::GetTrackedId() const
{
	const UUEGT2SurveySubsystem* Service = Survey.Get();
	return Service ? Service->GetTrackedLandmarkId() : NAME_None;
}

#undef LOCTEXT_NAMESPACE
