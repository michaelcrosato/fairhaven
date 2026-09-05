#include "UI/SUEGT2RestPanel.h"

#include "Interaction/UEGT2Amenity.h"
#include "Player/UEGT2PlayerController.h"
#include "Rest/UEGT2RestSubsystem.h"
#include "Rest/UEGT2RestTypes.h"
#include "UI/UEGT2UIStyle.h"
#include "World/UEGT2Almanac.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEGT2RestPanel"

void SUEGT2RestPanel::Construct(const FArguments& InArgs)
{
	using namespace UEGT2UI;
	Controller = InArgs._Controller;
	Bed = InArgs._Bed;
	OnClose = InArgs._OnClose;
	Rest = UUEGT2RestSubsystem::Get(Controller.IsValid() ? Controller->GetWorld() : nullptr);
	RefreshPreview();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(LOCTEXT("Title", "Sleep until"), 30, Ink, "Bold")
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(this, &SUEGT2RestPanel::GetPreviewVisibility)
			.Text_Lambda([this] { return CurrentTimeText; })
			.Font(Font("Regular", 12))
			.ColorAndOpacity(FSlateColor(Muted))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 22.0f, 0.0f, 18.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(112.0f).HeightOverride(44.0f)
				[
					SNew(SButton)
					.ButtonStyle(&ButtonStyle())
					.HAlign(HAlign_Center)
					.IsEnabled(this, &SUEGT2RestPanel::CanChooseHour)
					.ToolTipText(LOCTEXT("Earlier", "One hour earlier"))
					.OnClicked_Lambda([this] { return ChangeHour(-1); })
					[ Label(LOCTEXT("PreviousHour", "< Earlier"), 13, Ink, "Bold") ]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return UEGT2FormatClock(static_cast<float>(WakeHour)); })
				.Font(Font("Bold", 28))
				.ColorAndOpacity(FSlateColor(Accent))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(112.0f).HeightOverride(44.0f)
				[
					SNew(SButton)
					.ButtonStyle(&ButtonStyle())
					.HAlign(HAlign_Center)
					.IsEnabled(this, &SUEGT2RestPanel::CanChooseHour)
					.ToolTipText(LOCTEXT("Later", "One hour later"))
					.OnClicked_Lambda([this] { return ChangeHour(1); })
					[ Label(LOCTEXT("NextHour", "Later >"), 13, Ink, "Bold") ]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.Visibility(this, &SUEGT2RestPanel::GetPreviewVisibility)
			.BorderImage(Box())
			.BorderBackgroundColor(FSlateColor(Well))
			.Padding(14.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([this] { return DurationText; })
					.Font(Font("Bold", 15))
					.ColorAndOpacity(FSlateColor(Accent))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this] { return WakeText; })
					.Font(Font("Regular", 13))
					.ColorAndOpacity(FSlateColor(Ink))
					.AutoWrapText(true)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Consequences", "Sleep restores energy. You still grow hungry and need the washroom and company. You earn no coins while asleep, and the town continues its day."))
			.Font(Font("Regular", 12))
			.ColorAndOpacity(FSlateColor(Muted))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(this, &SUEGT2RestPanel::GetStatusText)
			.Font(Font("Regular", 12))
			.ColorAndOpacity(FSlateColor(Accent))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 20.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(170.0f).HeightOverride(40.0f)
				[
					SAssignNew(CancelButton, SButton)
					.ButtonStyle(&ButtonStyle())
					.HAlign(HAlign_Center)
					.OnClicked_Lambda([this]
					{
						OnClose.ExecuteIfBound();
						return FReply::Handled();
					})
					[ Label(LOCTEXT("Cancel", "Cancel"), 13) ]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(170.0f).HeightOverride(40.0f)
				[
					SNew(SButton)
					.ButtonStyle(&ButtonStyle())
					.HAlign(HAlign_Center)
					.IsEnabled(this, &SUEGT2RestPanel::CanConfirmSleep)
					.OnClicked(this, &SUEGT2RestPanel::Sleep)
					[ Label(LOCTEXT("Sleep", "Sleep"), 13) ]
				]
			]
		]
	];
}

TSharedPtr<SWidget> SUEGT2RestPanel::GetInitialFocusWidget() const
{
	return CancelButton;
}

void SUEGT2RestPanel::RefreshPreview()
{
	// Paused time does not change between input events. The service owns every
	// calendar calculation; Slate only formats this one consistent snapshot.
	bPreviewValid = false;
	StatusText = FText::GetEmpty();
	if (!CanSleepAtBed(StatusText)) { return; }
	FUEGT2RestPreview Preview;
	if (!Rest->GetPreview(Controller.Get(), Bed.Get(), WakeHour, Preview, StatusText)) { return; }
	bPreviewValid = true;

	const FUEGT2Date StartDate = UEGT2DateFromDayIndex(Preview.StartDayIndex);
	CurrentTimeText = FText::Format(LOCTEXT("CurrentTime", "Now: {0} on {1}, year {2}"),
		UEGT2FormatClock(Preview.StartHour), UEGT2FormatDate(StartDate), FText::AsNumber(StartDate.Year));
	const int32 Seconds = FMath::RoundToInt(Preview.DurationHours * 3600.0f);
	DurationText = Seconds > 0
		? FText::Format(LOCTEXT("Duration", "Sleep for {0} h {1} min {2} sec"),
			FText::AsNumber(Seconds / 3600), FText::AsNumber((Seconds / 60) % 60), FText::AsNumber(Seconds % 60))
		: LOCTEXT("LessThanASecond", "Sleep for less than 1 second");
	const FUEGT2Date WakeDate = UEGT2DateFromDayIndex(Preview.WakeDayIndex);
	WakeText = FText::Format(Preview.WakeDayIndex == Preview.StartDayIndex
		? LOCTEXT("WakeToday", "Wake today at {0} on {1}, year {2}")
		: LOCTEXT("WakeTomorrow", "Wake tomorrow at {0} on {1}, year {2}"),
		UEGT2FormatClock(static_cast<float>(Preview.WakeHour)), UEGT2FormatDate(WakeDate), FText::AsNumber(WakeDate.Year));
}

FReply SUEGT2RestPanel::ChangeHour(int32 Delta)
{
	if (CanChooseHour())
	{
		WakeHour = (WakeHour + Delta + 24) % 24;
		RefreshPreview();
	}
	return FReply::Handled();
}

FReply SUEGT2RestPanel::Sleep()
{
	if (bSubmitting) { return FReply::Handled(); }
	if (!CanSleepAtBed(StatusText)) { return FReply::Handled(); }
	bSubmitting = true;
	// Commit revalidates the bed, switches and clock instead of trusting the
	// preview the player saw before clicking. Failure keeps the panel open.
	if (Rest->SleepUntil(Controller.Get(), Bed.Get(), WakeHour, StatusText))
	{
		OnClose.ExecuteIfBound();
	}
	else
	{
		bSubmitting = false;
		if (StatusText.IsEmpty())
		{
			StatusText = LOCTEXT("Failed", "You cannot sleep until that hour right now.");
		}
	}
	return FReply::Handled();
}

bool SUEGT2RestPanel::CanSleepAtBed(FText& Reason) const
{
	const UUEGT2RestSubsystem* Service = Rest.Get();
	if (!Service || !Controller.IsValid() || !Bed.IsValid())
	{
		Reason = LOCTEXT("Unavailable", "These lodgings are no longer available. Cancel to return to your visit.");
		return false;
	}
	return Service->CanSleepAt(Controller.Get(), Bed.Get(), Reason);
}

bool SUEGT2RestPanel::CanChooseHour() const
{
	FText Reason;
	return !bSubmitting && CanSleepAtBed(Reason);
}

bool SUEGT2RestPanel::CanConfirmSleep() const
{
	return bPreviewValid && CanChooseHour();
}

FText SUEGT2RestPanel::GetStatusText() const
{
	FText Reason;
	return CanSleepAtBed(Reason) ? StatusText : Reason;
}

EVisibility SUEGT2RestPanel::GetPreviewVisibility() const
{
	FText Reason;
	return bPreviewValid && CanSleepAtBed(Reason) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
