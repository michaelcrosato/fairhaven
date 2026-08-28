#include "UI/SUEGT2Dialogue.h"

#include "NPC/UEGT2NPCActor.h"
#include "Player/UEGT2PlayerController.h"
#include "UI/UEGT2UIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEGT2Dialogue"

using namespace UEGT2UI;

namespace
{
	/** A thin bar showing one need. Green when met, amber low, red urgent. */
	TSharedRef<SWidget> NeedBar(const FText& Caption, float Value)
	{
		const FLinearColor Fill = Value > 0.55f ? FLinearColor(0.45f, 0.78f, 0.52f, 1.0f)
			: Value > 0.26f ? FLinearColor(0.90f, 0.74f, 0.36f, 1.0f)
			: FLinearColor(0.88f, 0.42f, 0.38f, 1.0f);

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Label(Caption, 10, Muted)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(84.0f).HeightOverride(6.0f)
				[
					SNew(SBorder)
					.BorderImage(Box())
					.BorderBackgroundColor(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f)))
					.Padding(0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(FMath::Max(Value, 0.02f))
						[
							SNew(SBorder)
							.BorderImage(Box())
							.BorderBackgroundColor(FSlateColor(Fill))
							.Padding(0.0f)
						]
						+ SHorizontalBox::Slot().FillWidth(FMath::Max(1.0f - Value, 0.0f))
						[
							SNew(SSpacer)
						]
					]
				]
			];
	}
}

// ---------------------------------------------------------------------------
void SUEGT2Dialogue::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	ChildSlot
	[
		// Bottom of the screen, not the middle: you are talking to somebody who
		// is standing in front of you, and a panel over their face would be a
		// strange way to hold a conversation.
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SSpacer)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		  .Padding(0.0f, 0.0f, 0.0f, 46.0f)
		[
			SNew(SBox).WidthOverride(1120.0f)
			[
				SNew(SBorder)
				.BorderImage(Box())
				.BorderBackgroundColor(FSlateColor(SolidPanel))
				.Padding(FMargin(26.0f, 20.0f))
				[
					SNew(SVerticalBox)

					// --- who you are talking to, and how they are ------------
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this] { return NameText; })
							.Font(Font("Bold", 20))
							.ColorAndOpacity(FSlateColor(Voice))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						  .Padding(12.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this] { return RoleText; })
							.Font(Font("Regular", 12))
							.ColorAndOpacity(FSlateColor(Muted))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SSpacer)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this] { return StatusText; })
							.Font(Font("Italic", 12))
							.ColorAndOpacity(FSlateColor(Muted))
						]
					]

					// --- the transcript -------------------------------------
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(Box())
						.BorderBackgroundColor(FSlateColor(Well))
						.Padding(FMargin(14.0f, 10.0f))
						[
							SNew(SBox).HeightOverride(168.0f)
							[
								SAssignNew(TranscriptScroll, SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(Transcript, SVerticalBox)
								]
							]
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f)
					[
						SNew(SBox).HeightOverride(1.0f)
						[
							SNew(SBorder).BorderImage(Box())
							.BorderBackgroundColor(FSlateColor(Divider)).Padding(0.0f)
						]
					]

					// --- what you can say -----------------------------------
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(TopicList, SVerticalBox)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
					[
						Label(LOCTEXT("DialogueHint",
							"Number keys choose  -  Esc ends the conversation"), 11, Muted)
					]
				]
			]
		]
	];
}

// ---------------------------------------------------------------------------
void SUEGT2Dialogue::SetPartner(AUEGT2NPCActor* InPartner)
{
	Partner = InPartner;
	if (Transcript.IsValid())
	{
		Transcript->ClearChildren();
	}
	if (InPartner == nullptr)
	{
		return;
	}

	const FUEGT2DialogueState State = InPartner->MakeDialogueState();
	const FText Greeting = UEGT2DialogueGreeting(State);
	AddLine(State.DisplayName, Greeting, false);
	InPartner->SayReply(Greeting);
	Refresh();
}

// ---------------------------------------------------------------------------
void SUEGT2Dialogue::Refresh()
{
	AUEGT2NPCActor* NPC = Partner.Get();
	if (NPC == nullptr || !TopicList.IsValid())
	{
		return;
	}

	const FUEGT2DialogueState State = NPC->MakeDialogueState();
	NameText = State.DisplayName;
	RoleText = GetRoleDisplayName(State.Role);
	StatusText = State.bFollowing
		? FText::Format(LOCTEXT("StatusFollowing", "{0}  -  walking with you"),
			GetActivityDisplayName(State.Activity))
		: GetActivityDisplayName(State.Activity);
	Shown = State.Needs;

	TArray<FUEGT2DialogueOption> Options;
	UEGT2DialogueTopics(State, Options);

	Ordered.Reset();
	TopicList->ClearChildren();

	// Two columns. Eleven topics in one column is a wall; in two it is a menu
	// you can read at a glance, which is the whole job of this panel.
	TSharedRef<SHorizontalBox> Columns = SNew(SHorizontalBox);
	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
	const int32 Half = (Options.Num() + 1) / 2;

	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		const FUEGT2DialogueOption& Option = Options[Index];
		Ordered.Add(Option.Topic);

		const bool bAction = UEGT2DialogueIsAction(Option.Topic);
		const bool bEnd = Option.Topic == EUEGT2DialogueTopic::Farewell;
		const FLinearColor Colour = bAction ? Accent : (bEnd ? Muted : Ink);
		const EUEGT2DialogueTopic Topic = Option.Topic;

		TSharedRef<SWidget> Row =
			SNew(SButton)
			.ButtonStyle(&ButtonStyle())
			.HAlign(HAlign_Left)
			.OnClicked(FOnClicked::CreateSP(this, &SUEGT2Dialogue::OnTopic, Topic))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				  .Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					// Only the first ten are numbered. Wrapping round to "1"
					// again gave two buttons the same key and neither worked.
					SNew(SBox).WidthOverride(14.0f)
					[
						SNew(STextBlock)
						.Text(Index < 10 ? FText::AsNumber((Index + 1) % 10) : FText::GetEmpty())
						.Font(Font("Bold", 12))
						.ColorAndOpacity(FSlateColor(Accent))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Option.Prompt)
					.Font(Font(bAction ? "Bold" : "Regular", 13))
					.ColorAndOpacity(FSlateColor(Colour))
				]
			];

		(Index < Half ? Left : Right)->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[ Row ];
	}

	Columns->AddSlot().FillWidth(1.0f).Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Left ];
	Columns->AddSlot().FillWidth(1.0f).Padding(8.0f, 0.0f, 0.0f, 0.0f)[ Right ];

	TopicList->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
		  .Padding(0.0f, 0.0f, 22.0f, 0.0f)
		[
			// The needs, beside the topics: you can see how somebody is before
			// you ask, and then hear them say the same thing. That the two
			// agree is the point of the whole system.
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				NeedBar(LOCTEXT("BarRested", "Rested"), Shown.Energy)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				NeedBar(LOCTEXT("BarFed", "Fed"), Shown.Fed)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				NeedBar(LOCTEXT("BarComfort", "Comfort"), Shown.Relief)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				NeedBar(LOCTEXT("BarCompany", "Company"), Shown.Company)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			Columns
		]
	];
}

// ---------------------------------------------------------------------------
void SUEGT2Dialogue::AddLine(const FText& Speaker, const FText& Line, bool bPlayerSpoke)
{
	if (!Transcript.IsValid())
	{
		return;
	}

	Transcript->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
		  .Padding(0.0f, 1.0f, 12.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(122.0f)
			[
				SNew(STextBlock)
				.Text(Speaker)
				.Font(Font("Bold", 12))
				.ColorAndOpacity(FSlateColor(bPlayerSpoke ? Accent : Voice))
				.Justification(ETextJustify::Right)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(STextBlock)
			.Text(Line)
			.Font(Font("Regular", 14))
			.ColorAndOpacity(FSlateColor(bPlayerSpoke ? Muted : Ink))
			.AutoWrapText(true)
		]
	];

	if (TranscriptScroll.IsValid())
	{
		TranscriptScroll->ScrollToEnd();
	}
}

// ---------------------------------------------------------------------------
FReply SUEGT2Dialogue::OnTopic(EUEGT2DialogueTopic Topic)
{
	AUEGT2NPCActor* NPC = Partner.Get();
	AUEGT2PlayerController* Player = Controller.Get();
	if (NPC == nullptr)
	{
		return FReply::Handled();
	}

	AddLine(LOCTEXT("You", "You"), UEGT2DialoguePrompt(Topic), true);

	const FUEGT2DialogueState State = NPC->MakeDialogueState();
	const FText Answer = UEGT2DialogueAnswer(State, Topic);
	AddLine(State.DisplayName, Answer, false);
	NPC->SayReply(Answer);

	// The two topics that change the world rather than describe it.
	if (Topic == EUEGT2DialogueTopic::Follow)
	{
		NPC->SetFollowTarget(Player ? Player->GetPawn() : nullptr);
	}
	else if (Topic == EUEGT2DialogueTopic::Dismiss)
	{
		NPC->SetFollowTarget(nullptr);
	}
	else if (Topic == EUEGT2DialogueTopic::Farewell)
	{
		if (Player)
		{
			Player->CloseDialogue();
		}
		return FReply::Handled();
	}

	Refresh();
	return FReply::Handled();
}

// ---------------------------------------------------------------------------
FReply SUEGT2Dialogue::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		if (AUEGT2PlayerController* Player = Controller.Get())
		{
			Player->CloseDialogue();
		}
		return FReply::Handled();
	}

	// Number keys pick a topic, in the order they are listed.
	for (int32 Index = 0; Index < Ordered.Num() && Index < 10; ++Index)
	{
		const int32 Digit = (Index + 1) % 10;
		if (Key == FKey(*FString::Printf(TEXT("%d"), Digit)))
		{
			return OnTopic(Ordered[Index]);
		}
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
