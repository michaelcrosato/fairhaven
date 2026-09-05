// Fairhaven - the generated sign where the town survey contract is checked.
#pragma once

#include "CoreMinimal.h"
#include "Interaction/UEGT2InteractableActor.h"
#include "UEGT2SurveyContract.generated.h"

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2SurveyContract : public AUEGT2InteractableActor
{
	GENERATED_BODY()

public:
	AUEGT2SurveyContract();
	virtual bool CanInteract(const AActor* Interactor) const override;
	float GetUseRange() const { return UseRange; }
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Contract")
	void SetUseRange(float Range) { UseRange = Range; }

protected:
	virtual void OnInteract(AActor* Interactor) override;

private:
	UPROPERTY(EditAnywhere, Category = "UEGT2|Contract") float UseRange = 340.0f;
};
