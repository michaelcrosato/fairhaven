#include "UI/SUEGT2SurveyContract.h"

#include "Contracts/UEGT2SurveyContract.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Player/UEGT2PlayerController.h"
#include "Types/NavigationMetaData.h"
#include "UI/UEGT2UIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEGT2SurveyContract"

namespace UEGT2SurveyContractUI
{
	FText DescribeDirection(const FUEGT2SurveyContractEntry& Entry)
	{
		if (!Entry.bAvailable) { return LOCTEXT("Missing", "This survey marker is unavailable."); }
		if (!Entry.bHasDirection) { return LOCTEXT("NoDirection", "Direction unavailable."); }
		const float Metres = Entry.Direction.DistanceMetres;
		FNumberFormattingOptions Format;
		Format.SetMaximumFractionalDigits(Metres >= 1000.0f ? 1 : 0);
		const FText Distance = FText::Format(Metres >= 1000.0f
			? LOCTEXT("Kilometres", "{0} km") : LOCTEXT("Metres", "{0} m"),
			FText::AsNumber(Metres >= 1000.0f ? Metres / 1000.0f : Metres, &Format));
		return FText::Format(LOCTEXT("Direction", "{0} · {1} · straight line"), Distance, Entry.Direction.CompassDirection);
	}
}

void SUEGT2SurveyContract::Construct(const FArguments& InArgs)
{
	using namespace UEGT2UI;
	Controller = InArgs._Controller;
	Board = InArgs._Board;
	OnClose = InArgs._OnClose;
	Contract = UUEGT2SurveyContractSubsystem::Get(Controller.IsValid() ? Controller->GetWorld() : nullptr);
	// The page pauses play. Resolve the three markers and format their distances
	// once; the payment transaction revalidates the live world on every click.
	const TArray<FUEGT2SurveyContractEntry> Entries = Contract.IsValid()
		? Contract->GetEntries(Controller.Get()) : TArray<FUEGT2SurveyContractEntry>();
	if (Contract.IsValid()) { bReadyToClaim = Contract->CanClaim(Controller.Get(), Board.Get(), StatusText); }
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FUEGT2SurveyContractEntry& Entry : Entries)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f) [ BuildRow(Entry) ];
	}
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[ Label(LOCTEXT("Title", "Town Survey Contract"), 30, Ink, "Bold") ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 14.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]
			{
				return CanUseBoard()
					? LOCTEXT("Hint", "Survey these three places at their markers, then return to this signpost for payment. Places you have already surveyed count. There is no deadline.")
					: LOCTEXT("Unavailable", "This contract is turned off or unavailable here. Your surveys and any payment already earned are kept.");
			})
			.Font(Font("Regular", 12)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(240.0f)
			[ SNew(SScrollBox) + SScrollBox::Slot() [ Rows ] ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("Reward", "Payment: {0} coins, once per journey."), FText::AsNumber(UUEGT2SurveyContractSubsystem::GetReward())))
			.Font(Font("Bold", 14)).ColorAndOpacity(FSlateColor(Accent)).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text_Lambda([this] { return StatusText; })
			.Font(Font("Regular", 12)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true)
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
					SAssignNew(ClaimButton, SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center)
					.IsEnabled_Lambda([this] { return bReadyToClaim && CanUseBoard() && !Contract->IsPaid(); })
					.OnClicked(this, &SUEGT2SurveyContract::ClaimPayment)
					[
						SNew(STextBlock).Text_Lambda([this]
						{
							return Contract.IsValid() && Contract->IsPaid() ? LOCTEXT("PaidButton", "Paid") : LOCTEXT("Claim", "Claim Payment");
						})
						.Font(Font("Bold", 13)).ColorAndOpacity(FSlateColor(Ink))
					]
				]
			]
		]
	];
	TSharedRef<FNavigationMetaData> ResumeNav = MakeShared<FNavigationMetaData>();
	ResumeNav->SetNavigationExplicit(EUINavigation::Right, ClaimButton);
	ResumeButton->AddMetadata(ResumeNav);
	TSharedRef<FNavigationMetaData> ClaimNav = MakeShared<FNavigationMetaData>();
	ClaimNav->SetNavigationExplicit(EUINavigation::Left, ResumeButton);
	ClaimButton->AddMetadata(ClaimNav);
}

TSharedRef<SWidget> SUEGT2SurveyContract::BuildRow(const FUEGT2SurveyContractEntry& Entry)
{
	using namespace UEGT2UI;
	return SNew(SBorder).BorderImage(Box()).BorderBackgroundColor(FSlateColor(Well)).Padding(FMargin(12.0f, 8.0f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(Entry.Name).Font(Font("Bold", 14))
			.ColorAndOpacity(FSlateColor(Ink)).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(FText::Format(LOCTEXT("RowDetail", "{0} · {1}"),
				Entry.bDiscovered ? LOCTEXT("Surveyed", "Surveyed") : LOCTEXT("NotSurveyed", "Not yet surveyed"),
				UEGT2SurveyContractUI::DescribeDirection(Entry)))
			.Font(Font("Regular", 11)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true)
		]
	];
}

TSharedPtr<SWidget> SUEGT2SurveyContract::GetInitialFocusWidget() const { return ResumeButton; }

bool SUEGT2SurveyContract::CanUseBoard() const
{
	FText Reason;
	return Contract.IsValid() && Contract->CanOpenAt(Controller.Get(), Board.Get(), Reason);
}

FReply SUEGT2SurveyContract::ClaimPayment()
{
	if (Contract.IsValid() && Contract->TryClaim(Controller.Get(), Board.Get(), StatusText))
	{
		bReadyToClaim = false;
		StatusText = FText::Format(LOCTEXT("PaymentReceived", "Paid {0} coins. Thank you for surveying Fairhaven."),
			FText::AsNumber(UUEGT2SurveyContractSubsystem::GetReward()));
		return FReply::Handled().SetUserFocus(ResumeButton.ToSharedRef());
	}
	if (!Contract.IsValid()) { StatusText = LOCTEXT("NoContract", "This contract is no longer available."); }
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
