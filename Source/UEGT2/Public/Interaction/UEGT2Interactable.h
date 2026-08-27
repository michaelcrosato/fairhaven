// Fairhaven (UEGT2) - the one contract between the player and the world.
//
// Anything usable implements this. The interaction component never knows about
// concrete actor types, so adding a new usable object means implementing this
// interface and nothing else.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UEGT2Interactable.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UUEGT2Interactable : public UInterface
{
	GENERATED_BODY()
};

class UEGT2_API IUEGT2Interactable
{
	GENERATED_BODY()

public:
	/** Short verb shown in the HUD, e.g. "Open door". */
	virtual FText GetInteractionPrompt(const AActor* Interactor) const = 0;

	/** Perform the interaction. Only called when CanInteract returned true. */
	virtual void Interact(AActor* Interactor) = 0;

	/** False hides the prompt and blocks use (locked, already taken, ...). */
	virtual bool CanInteract(const AActor* Interactor) const { return true; }

	/** Called when the player starts or stops looking at this object. */
	virtual void SetInteractionFocus(bool bFocused) {}

	/** Optional world-space point the prompt should hang off. */
	virtual FVector GetInteractionPoint() const { return FVector::ZeroVector; }
};
