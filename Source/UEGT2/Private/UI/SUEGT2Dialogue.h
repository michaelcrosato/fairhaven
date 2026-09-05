// Fairhaven (UEGT2) - the conversation panel.
//
// Slate, like the rest of the UI, so the whole thing stays in readable C++ with
// no binary widget assets. It sits low on the screen rather than filling it:
// you are talking to somebody who is standing in front of you, and covering
// them up with a menu would be a strange way to have a conversation.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2Dialogue.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AUEGT2NPCActor;
class AUEGT2PlayerController;
class SScrollBox;
class SVerticalBox;

class SUEGT2Dialogue : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEGT2Dialogue) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2PlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Point the panel at somebody. Null closes it. */
	void SetPartner(AUEGT2NPCActor* InPartner);

	/** Ask one thing, as if the button had been clicked. */
	void AskTopic(EUEGT2DialogueTopic Topic) { OnTopic(Topic); }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;

private:
	/** Rebuild the available topics when the partner or a conversation action changes. */
	void Refresh();
	void UpdateState(const FUEGT2DialogueState& State);

	/** Ask one thing. Actions (follow, dismiss) also change the world. */
	FReply OnTopic(EUEGT2DialogueTopic Topic);

	void AddLine(const FText& Speaker, const FText& Line, bool bPlayerSpoke);

	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	TWeakObjectPtr<AUEGT2NPCActor> Partner;

	TSharedPtr<SVerticalBox> TopicList;
	TSharedPtr<SVerticalBox> Transcript;
	TSharedPtr<SScrollBox> TranscriptScroll;

	/** Header fields, updated every Refresh so the needs read live. */
	FText NameText;
	FText RoleText;
	FText StatusText;

	/** The four needs, 0..1, for the little bars beside the topic list. */
	FUEGT2NPCNeeds Shown;

	/** Topic order as built, so the number keys can select them. */
	TArray<EUEGT2DialogueTopic> Ordered;
	float RefreshCountdown = 0.0f;
	bool bShownFollowing = false;
};
