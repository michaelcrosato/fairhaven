// Fairhaven - the paused survey journal, hosted by the existing menu panel.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AUEGT2PlayerController;
class UUEGT2SurveySubsystem;
struct FUEGT2SurveyEntry;

class SUEGT2SurveyJournal : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEGT2SurveyJournal) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2PlayerController>, Controller)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildRow(const FUEGT2SurveyEntry& Entry);
	bool IsJournalEnabled() const;
	FName GetTrackedId() const;

	TWeakObjectPtr<UUEGT2SurveySubsystem> Survey;
	FSimpleDelegate OnClose;
};
