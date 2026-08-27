// Fairhaven (UEGT2) - camera-forward interaction probe.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UEGT2InteractionComponent.generated.h"

class IUEGT2Interactable;

/** Fired when the focused interactable changes. Actor may be null. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FUEGT2FocusChanged, AActor* /*Focused*/, const FText& /*Prompt*/);

UCLASS(ClassGroup = "UEGT2", meta = (BlueprintSpawnableComponent))
class UEGT2_API UUEGT2InteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUEGT2InteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Use whatever is currently focused. Returns true if something happened. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Interaction")
	bool TryInteract();

	UFUNCTION(BlueprintPure, Category = "UEGT2|Interaction")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Interaction")
	FText GetFocusedPrompt() const { return FocusedPrompt; }

	FUEGT2FocusChanged OnFocusChanged;

	/** How far the player can reach, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Interaction")
	float Reach = 320.0f;

	/** Sphere sweep radius; a little forgiveness makes small props usable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Interaction")
	float ProbeRadius = 12.0f;

private:
	void UpdateFocus();
	AActor* ProbeForInteractable(FText& OutPrompt) const;
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	TWeakObjectPtr<AActor> FocusedActor;
	FText FocusedPrompt;
};
