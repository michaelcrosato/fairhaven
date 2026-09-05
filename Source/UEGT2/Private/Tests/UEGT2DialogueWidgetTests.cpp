#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "NPC/UEGT2NPCActor.h"
#include "UI/SUEGT2Dialogue.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace UEGT2DialogueWidgetTests
{
	TSharedPtr<SWidget> FindText(const TSharedRef<SWidget>& Widget, const FString& Text)
	{
		if (Widget->GetTypeAsString() == TEXT("STextBlock")
			&& StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Text)
		{
			return Widget;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindText(Children->GetChildAt(Index), Text))
			{
				return Found;
			}
		}
		return nullptr;
	}

	void FindNeedBars(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SHorizontalBox>>& Bars)
	{
		FChildren* Children = Widget->GetChildren();
		if (Widget->GetTypeAsString() == TEXT("SHorizontalBox") && Children->Num() == 2
			&& Children->GetChildAt(0)->GetTypeAsString() == TEXT("SBorder")
			&& Children->GetChildAt(1)->GetTypeAsString() == TEXT("SSpacer"))
		{
			Bars.Add(StaticCastSharedRef<SHorizontalBox>(Widget));
		}
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			FindNeedBars(Children->GetChildAt(Index), Bars);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2DialogueWidgetTest,
	"UEGT2.Dialogue.WidgetInputAndLiveState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2DialogueWidgetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("test world exists"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AUEGT2NPCActor* NPC = World->SpawnActor<AUEGT2NPCActor>();
	if (!TestNotNull(TEXT("conversation partner exists"), NPC))
	{
		return false;
	}
	NPC->ConfigureNPC(TEXT("Before"), EUEGT2NPCRole::Villager, EUEGT2NPCSpecies::Person, 42);
	TSharedRef<SUEGT2Dialogue> Panel = SNew(SUEGT2Dialogue);
	Panel->SetPartner(NPC);
	TestTrue(TEXT("the panel shows its partner"), UEGT2DialogueWidgetTests::FindText(Panel, TEXT("Before")).IsValid());

	// Exercise the widget's real input handler with the keys Unreal emits.
	// Their internal names are One/Two/etc., even though their labels are digits.
	const FKey Keys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
	for (const FKey& Key : Keys)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		TestTrue(FString::Printf(TEXT("%s chooses a topic"), *Key.ToString()),
			Panel->OnKeyDown(FGeometry(), Event).IsEventHandled());
	}
	const FKeyEvent Other(EKeys::A, FModifierKeysState(), 0, false, 0, 0);
	TestFalse(TEXT("unrelated keys are not topics"), Panel->OnKeyDown(FGeometry(), Other).IsEventHandled());

	// A state change must appear without another click, while retaining the
	// existing button tree (otherwise a refresh can steal a pending click).
	// Follow is not among the ten numbered questions, so this finds its button
	// label rather than an earlier copy of the question in the transcript.
	const FString TopicText = UEGT2DialoguePrompt(EUEGT2DialogueTopic::Follow).ToString();
	const TSharedPtr<SWidget> Topic = UEGT2DialogueWidgetTests::FindText(Panel, TopicText);
	TestTrue(TEXT("a topic button is present"), Topic.IsValid());
	NPC->ConfigureNPC(TEXT("After"), EUEGT2NPCRole::Baker, EUEGT2NPCSpecies::Person, 42);
	Panel->Tick(FGeometry(), 1.0, 0.25f);
	TestTrue(TEXT("the visible header follows live state"), UEGT2DialogueWidgetTests::FindText(Panel, TEXT("After")).IsValid());
	TestTrue(TEXT("live refresh keeps the topic buttons"), UEGT2DialogueWidgetTests::FindText(Panel, TopicText) == Topic);

	TArray<TSharedRef<SHorizontalBox>> Bars;
	UEGT2DialogueWidgetTests::FindNeedBars(Panel, Bars);
	if (TestEqual(TEXT("all four needs have visible bars"), Bars.Num(), 4))
	{
		Panel->SlatePrepass(1.0f);
		const float Before = Bars[0]->GetSlot(0).GetSizeValue();
		const TSharedRef<SBorder> Fill = StaticCastSharedRef<SBorder>(Bars[0]->GetChildren()->GetChildAt(0));
		const FLinearColor BeforeColour = Fill->GetBorderBackgroundColor().GetSpecifiedColor();
		NPC->AdvanceNeeds(10.0f);
		Panel->Tick(FGeometry(), 1.25, 0.25f);
		Panel->SlatePrepass(1.0f);
		TestTrue(TEXT("rested bar shrinks without asking another question"),
			Bars[0]->GetSlot(0).GetSizeValue() < Before);
		TestNotEqual(TEXT("rested bar colour follows the depleted need"),
			Fill->GetBorderBackgroundColor().GetSpecifiedColor(), BeforeColour);
	}
	return true;
}

#endif
