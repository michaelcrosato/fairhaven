// Fairhaven (UEGT2) - base class for everything the player can use.
//
// Handles the mesh, the prompt, focus and single-use bookkeeping. Subclasses
// only implement OnInteract. Adding a new usable object should not require
// touching the player, the HUD or the interaction component.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/UEGT2Interactable.h"
#include "UEGT2InteractableActor.generated.h"

class UStaticMeshComponent;

UCLASS(Abstract, ClassGroup = "UEGT2")
class UEGT2_API AUEGT2InteractableActor : public AActor, public IUEGT2Interactable
{
	GENERATED_BODY()

public:
	AUEGT2InteractableActor();

	// --- IUEGT2Interactable ------------------------------------------------
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;
	virtual bool CanInteract(const AActor* Interactor) const override;
	virtual void Interact(AActor* Interactor) override final;
	virtual void SetInteractionFocus(bool bFocused) override;
	virtual FVector GetInteractionPoint() const override;

	/** Assign the visual mesh. Called by the content build. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Interaction")
	void SetInteractableMesh(UStaticMesh* Mesh);

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Interaction")
	void SetPromptText(const FText& Text) { PromptText = Text; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Interaction")
	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

protected:
	/** Subclass hook: the actual behaviour. */
	virtual void OnInteract(AActor* Interactor) PURE_VIRTUAL(AUEGT2InteractableActor::OnInteract, );

	/** Convenience: put a line on the player's HUD. */
	void ShowHudMessage(AActor* Interactor, const FText& Message, float Duration = 4.0f) const;

	/** Verb shown in the HUD, e.g. "Read notice". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Interaction")
	FText PromptText;

	/** When true the object can only be used once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Interaction")
	bool bSingleUse = false;

	UPROPERTY(VisibleAnywhere, Category = "UEGT2")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	bool bUsed = false;
	bool bFocused = false;
};
