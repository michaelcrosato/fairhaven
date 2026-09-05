#include "UI/SUEGT2ServicesGuide.h"

#include "Interaction/UEGT2Amenity.h"
#include "Player/UEGT2PlayerController.h"
#include "Services/UEGT2ServicesSubsystem.h"
#include "Types/NavigationMetaData.h"
#include "UI/UEGT2UIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEGT2ServicesGuide"

namespace UEGT2ServicesGuide
{
	FText Describe(const FUEGT2ServiceEntry& Entry)
	{
		if (!Entry.Amenity.IsValid()) { return LOCTEXT("Missing", "No place found in this world."); }
		FNumberFormattingOptions Format;
		Format.SetMaximumFractionalDigits(Entry.DistanceMetres >= 1000.0f ? 1 : 0);
		const FText Distance = FText::Format(Entry.DistanceMetres >= 1000.0f
			? LOCTEXT("Kilometres", "{0} km") : LOCTEXT("Metres", "{0} m"),
			FText::AsNumber(Entry.DistanceMetres >= 1000.0f ? Entry.DistanceMetres / 1000.0f : Entry.DistanceMetres, &Format));
		const FText Rate = Entry.Category == EUEGT2ServiceCategory::PaidWork
			? FText::Format(LOCTEXT("Wage", "{0} · pays {1} coins/world hour"),
				GetRoleDisplayName(Entry.JobRole), FText::AsNumber(Entry.WagePerHour))
			: Entry.CostPerHour > 0.0f
				? FText::Format(LOCTEXT("Cost", "{0} coins/world hour"), FText::AsNumber(Entry.CostPerHour))
				: LOCTEXT("Free", "Free");
		return FText::Format(LOCTEXT("Detail", "{0} · {1}"), Distance, Rate);
	}
}

void SUEGT2ServicesGuide::Construct(const FArguments& InArgs)
{
	using namespace UEGT2UI;
	const AUEGT2PlayerController* PC = InArgs._Controller.Get();
	Services = UUEGT2ServicesSubsystem::Get(PC ? PC->GetWorld() : nullptr);
	OnClose = InArgs._OnClose;
	// This page pauses the world. Actor enumeration and rate formatting belong
	// to construction, never Slate's per-frame text/visibility attributes.
	const TArray<FUEGT2ServiceEntry> Entries = IsGuideEnabled()
		? Services->GetEntries(PC) : TArray<FUEGT2ServiceEntry>();
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FUEGT2ServiceEntry& Entry : Entries)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f) [ BuildRow(Entry) ];
	}
	if (Entries.IsEmpty())
	{
		Rows->AddSlot().AutoHeight().Padding(12.0f)
		[ Label(LOCTEXT("NotReady", "Nearby places are not available here yet."), 13, Muted) ];
	}
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[ Label(LOCTEXT("Title", "Nearby Services"), 30, Ink, "Bold") ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 16.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]
			{
				return IsGuideEnabled()
					? LOCTEXT("Hint", "Nearest places by straight-line distance, plus your home kitchen and bed. Tracking replaces your current directions. Walk there and use the normal interaction prompt.")
					: LOCTEXT("Disabled", "Nearby Services is turned off or unavailable. Every place is still usable, and your progress is unchanged.");
			})
			.Font(Font("Regular", 12)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(330.0f)
			.Visibility_Lambda([this] { return IsGuideEnabled() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll)
				+ SScrollBox::Slot() [ Rows ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text_Lambda([this] { return StatusText; })
			.Font(Font("Regular", 12)).ColorAndOpacity(FSlateColor(Accent)).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(170.0f).HeightOverride(40.0f)
				[
					SAssignNew(ResumeButton, SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center)
					.OnClicked_Lambda([this] { OnClose.ExecuteIfBound(); return FReply::Handled(); })
					[ Label(LOCTEXT("Resume", "Resume"), 13) ]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(170.0f).HeightOverride(40.0f)
				[
					SAssignNew(StopButton, SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center)
					.IsEnabled_Lambda([this] { return IsGuideEnabled() && Services->GetTrackedAmenity() != nullptr; })
					.OnClicked_Lambda([this]
					{
						if (IsGuideEnabled()) { Services->ClearTracking(); StatusText = FText::GetEmpty(); }
						return FReply::Handled().SetUserFocus(ResumeButton.ToSharedRef());
					})
					[ Label(LOCTEXT("StopTracking", "Stop Tracking"), 13) ]
				]
			]
		]
	];
	ConfigureNavigation();
}

TSharedRef<SWidget> SUEGT2ServicesGuide::BuildRow(const FUEGT2ServiceEntry& Entry)
{
	using namespace UEGT2UI;
	const TWeakObjectPtr<AUEGT2Amenity> Amenity = Entry.Amenity;
	const FText OriginalVenue = Amenity.IsValid() ? Amenity->GetVenueName() : FText::GetEmpty();
	TSharedRef<SWidget> Control = SNew(SSpacer).Size(FVector2D(112.0f, 36.0f));
	if (Amenity.IsValid())
	{
		TSharedPtr<SButton> TrackButton;
		Control = SNew(SBox).WidthOverride(112.0f).HeightOverride(36.0f)
		[
			SAssignNew(TrackButton, SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center)
			.IsEnabled_Lambda([this, Amenity] { return IsGuideEnabled() && Amenity.IsValid() && !Amenity->IsActorBeingDestroyed(); })
			.OnClicked_Lambda([this, Entry, OriginalVenue] { return Track(Entry, OriginalVenue); })
			[
				SNew(STextBlock)
				.Text_Lambda([this, Amenity] { return IsTracking(Amenity) ? LOCTEXT("Tracking", "Tracking") : LOCTEXT("Track", "Track"); })
				.Font(Font("Bold", 12)).ColorAndOpacity(FSlateColor(Ink))
			]
		];
		TrackButtons.Add(TrackButton);
	}
	return SNew(SBorder).BorderImage(Box()).BorderBackgroundColor(FSlateColor(Well)).Padding(FMargin(12.0f, 8.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Entry.Amenity.IsValid() && !Entry.CategoryName.EqualTo(Entry.Name)
					? FText::Format(LOCTEXT("Place", "{0} — {1}"), Entry.CategoryName, Entry.Name) : Entry.CategoryName)
				.Font(Font("Bold", 14)).ColorAndOpacity(FSlateColor(Ink)).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(UEGT2ServicesGuide::Describe(Entry))
				.Font(Font("Regular", 11)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center) [ Control ]
	];
}

void SUEGT2ServicesGuide::ConfigureNavigation()
{
	// Explicit links cross the scroll-box boundary even when the last row is
	// clipped. Weak metadata targets avoid retaining a closed page.
	TSharedRef<FNavigationMetaData> ResumeNav = MakeShared<FNavigationMetaData>();
	ResumeNav->SetNavigationExplicit(EUINavigation::Right, StopButton);
	TSharedRef<FNavigationMetaData> StopNav = MakeShared<FNavigationMetaData>();
	StopNav->SetNavigationExplicit(EUINavigation::Left, ResumeButton);
	if (!TrackButtons.IsEmpty())
	{
		ResumeNav->SetNavigationExplicit(EUINavigation::Up, TrackButtons.Last());
		ResumeNav->SetNavigationExplicit(EUINavigation::Down, TrackButtons[0]);
		StopNav->SetNavigationExplicit(EUINavigation::Up, TrackButtons.Last());
		StopNav->SetNavigationExplicit(EUINavigation::Down, TrackButtons[0]);
		for (int32 Index = 0; Index < TrackButtons.Num(); ++Index)
		{
			TSharedRef<FNavigationMetaData> Nav = MakeShared<FNavigationMetaData>();
			Nav->SetNavigationExplicit(EUINavigation::Up, Index == 0 ? ResumeButton : TrackButtons[Index - 1]);
			Nav->SetNavigationExplicit(EUINavigation::Down, Index + 1 == TrackButtons.Num() ? ResumeButton : TrackButtons[Index + 1]);
			Nav->SetNavigationExplicit(EUINavigation::Left, ResumeButton);
			TrackButtons[Index]->AddMetadata(Nav);
		}
	}
	ResumeButton->AddMetadata(ResumeNav);
	StopButton->AddMetadata(StopNav);
}

TSharedPtr<SWidget> SUEGT2ServicesGuide::GetInitialFocusWidget() const { return ResumeButton; }

bool SUEGT2ServicesGuide::IsGuideEnabled() const
{
	return Services.IsValid() && Services->IsEnabled();
}

bool SUEGT2ServicesGuide::CanTrack(const FUEGT2ServiceEntry& Entry, const FText& OriginalVenue) const
{
	const TWeakObjectPtr<AUEGT2Amenity> Amenity = Entry.Amenity;
	return IsGuideEnabled() && Amenity.IsValid() && !Amenity->IsActorBeingDestroyed()
		&& Amenity->GetWorld() == Services->GetWorld() && Amenity->GetActivity() == Entry.Activity
		&& Amenity->GetJobRole() == Entry.JobRole && Amenity->GetVenueName().EqualTo(OriginalVenue);
}

bool SUEGT2ServicesGuide::IsTracking(TWeakObjectPtr<AUEGT2Amenity> Amenity) const
{
	return Amenity.IsValid() && IsGuideEnabled() && Services->GetTrackedAmenity() == Amenity.Get();
}

FReply SUEGT2ServicesGuide::Track(const FUEGT2ServiceEntry& Entry, const FText& OriginalVenue)
{
	if (CanTrack(Entry, OriginalVenue) && Services->TrackAmenity(Entry.Amenity.Get())) { StatusText = FText::GetEmpty(); }
	else { StatusText = LOCTEXT("Unavailable", "That place is no longer available. Reopen the guide to refresh nearby places."); }
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
