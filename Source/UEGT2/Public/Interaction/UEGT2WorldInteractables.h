// Fairhaven (UEGT2) - the concrete usable objects in the 0.1 world.
//
// Deliberately small: a readable sign, a door that swings, a lamp that lights,
// a prop you can carry and throw, and a landmark that records a discovery.
// That is enough to playtest movement, reach, and physical interaction without
// committing to any gameplay systems.
#pragma once

#include "CoreMinimal.h"
#include "Interaction/UEGT2InteractableActor.h"
#include "UEGT2WorldInteractables.generated.h"

class UPointLightComponent;

/** A notice board or signpost that prints its text to the HUD. */
UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2Sign : public AUEGT2InteractableActor
{
	GENERATED_BODY()

public:
	AUEGT2Sign();

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Interaction")
	void SetSignText(const FText& Text) { SignText = Text; }

protected:
	virtual void OnInteract(AActor* Interactor) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sign")
	FText SignText;
};

/** A door that swings open and closed about one edge. No animation asset. */
UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2Door : public AUEGT2InteractableActor
{
	GENERATED_BODY()

public:
	AUEGT2Door();

	virtual void Tick(float DeltaSeconds) override;
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Door")
	float OpenAngle = 92.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Door")
	float SwingSpeed = 220.0f;

protected:
	virtual void OnInteract(AActor* Interactor) override;

private:
	bool bOpen = false;
	float CurrentAngle = 0.0f;
	float ClosedYaw = 0.0f;
};

/** A lamp the player can switch on and off. */
UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2Lamp : public AUEGT2InteractableActor
{
	GENERATED_BODY()

public:
	AUEGT2Lamp();

	virtual void BeginPlay() override;
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Lamp")
	bool bStartsOn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Lamp")
	float Brightness = 9000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Lamp")
	float Radius = 1400.0f;

protected:
	virtual void OnInteract(AActor* Interactor) override;

private:
	void ApplyState();

	UPROPERTY(VisibleAnywhere, Category = "UEGT2")
	TObjectPtr<UPointLightComponent> Light;

	bool bOn = true;
};

/** A physics prop the player can pick up, carry and throw. */
UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2Pickup : public AUEGT2InteractableActor
{
	GENERATED_BODY()

public:
	AUEGT2Pickup();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Pickup")
	float CarryDistance = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Pickup")
	float ThrowImpulse = 52000.0f;

	UFUNCTION(BlueprintPure, Category = "UEGT2|Pickup")
	bool IsCarried() const { return Carrier != nullptr; }

	/** End this carrier's transient interaction without throwing the prop. */
	bool ReleaseIfCarriedBy(AActor* Actor);

protected:
	virtual void OnInteract(AActor* Interactor) override;

private:
	void Drop(bool bThrow);

	UPROPERTY(Transient) TObjectPtr<AActor> Carrier = nullptr;
};

/** A viewpoint that records a discovery when the player reaches it. */
UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2Landmark : public AUEGT2InteractableActor
{
	GENERATED_BODY()

public:
	AUEGT2Landmark();

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Landmark")
	void SetLandmarkName(const FText& Text) { LandmarkName = Text; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Landmark")
	FText GetLandmarkName() const { return LandmarkName; }

	/** Authored identity for saves; independent of display text and placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Landmark")
	FName PersistentId = NAME_None;

	UFUNCTION(BlueprintPure, Category = "UEGT2|Landmark")
	FName GetPersistentId() const { return PersistentId; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Landmark")
	bool IsDiscovered() const { return bUsed; }

	/** Restore or reset discovery without replaying interaction feedback. */
	void SetDiscovered(bool bDiscovered);

	/** Counts belong to one world and reflect its current actors and state. */
	static int32 GetDiscoveredCount(const UWorld* World);
	static int32 GetTotalCount(const UWorld* World);

	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;

protected:
	virtual void OnInteract(AActor* Interactor) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Landmark")
	FText LandmarkName;
};
