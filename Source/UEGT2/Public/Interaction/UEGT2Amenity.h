// Fairhaven (UEGT2) - a place where the player can do what the town does.
//
// Every one of these stands on a point the NPC stage already resolves an
// anchor to: the food amenities are on the Food anchors, the seats on the Seat
// anchors, the work amenities on the workplaces the routines send people to.
// That is the whole design. A villager satisfies hunger by walking to the
// bakehouse doorstep and switching to Eat; the player walks to the same
// doorstep and presses use, and the same UEGT2AdvanceLife charges them both.
//
// It is deliberately an invisible volume with no mesh of its own. The bench,
// the privy and the stall are already standing there - the town stage put them
// there - and replacing those props with interactable copies would change
// their collision object type from WorldStatic to WorldDynamic, which is what
// the NPC ground trace queries by. An amenity is a *place*, not a prop.
#pragma once

#include "CoreMinimal.h"
#include "Interaction/UEGT2InteractableActor.h"
#include "NPC/UEGT2NPCTypes.h"
#include "UEGT2Amenity.generated.h"

class UBoxComponent;

/** What a place is for. One kind, one activity, one need it answers. */
UENUM(BlueprintType)
enum class EUEGT2AmenityKind : uint8
{
	Food,       // a counter, a stall, a bakehouse door: hunger
	Tavern,     // the inn: hunger and company, dearer than a meal
	Washroom,   // a privy or a public convenience: relief
	Seat,       // a bench: energy, and free
	Bed,        // your own bed: energy, fast and free
	Larder,     // your own kitchen: hunger, free, the meal the town eats at home
	Work,       // a wage, paid by the hour, at the trade the venue hires for
	Market,     // buying at a stall; work if you are the one selling
	Worship,    // the church: company
	Count UMETA(Hidden)
};

/** The activity somebody performs at this kind of place. */
UEGT2_API EUEGT2Activity UEGT2ActivityForAmenity(EUEGT2AmenityKind Kind);

/** Display name for the kind, for logs and the content build report. */
UEGT2_API const TCHAR* UEGT2AmenityKindName(EUEGT2AmenityKind Kind);

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2Amenity : public AUEGT2InteractableActor
{
	GENERATED_BODY()

public:
	AUEGT2Amenity();

	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;
	virtual FVector GetInteractionPoint() const override;

	// ---- Authoring (called from the content build) -------------------------
	/**
	 * What this place is, what it is called, and - for work - what it hires
	 * for. VenueName is what the prompt and the HUD say: "The Bakehouse".
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Amenity")
	void ConfigureAmenity(EUEGT2AmenityKind InKind, const FString& InVenueName,
		EUEGT2NPCRole InJobRole);

	/** How far the player may stray before the activity ends. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Amenity")
	void SetUseRange(float Range);
	float GetUseRange() const { return UseRange; }

	/** Half-extents of the volume the interaction probe can find. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Amenity")
	void SetVolumeExtent(const FVector& Extent);

	// ---- Queries -----------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "UEGT2|Amenity")
	EUEGT2AmenityKind GetKind() const { return Kind; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Amenity")
	FText GetVenueName() const { return VenueName; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Amenity")
	EUEGT2NPCRole GetJobRole() const { return JobRole; }

	/** The activity this place puts somebody into. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Amenity")
	EUEGT2Activity GetActivity() const { return UEGT2ActivityForAmenity(Kind); }

protected:
	virtual void OnInteract(AActor* Interactor) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|Amenity")
	EUEGT2AmenityKind Kind = EUEGT2AmenityKind::Seat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|Amenity")
	FText VenueName;

	/** Only read for Work and Market: the trade the wage is paid at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|Amenity")
	EUEGT2NPCRole JobRole = EUEGT2NPCRole::Villager;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Amenity")
	float UseRange = 420.0f;

private:
	/** "(5 an hour)" or "(pays 8 an hour)", or nothing when it is free. */
	FText MoneyNote() const;

	/**
	 * The thing the interaction probe actually hits.
	 *
	 * Query only, and answering the Visibility channel alone: the player has to
	 * be able to walk through it - it stands in a doorway and on top of a bench
	 * - and nothing but the probe should ever know it is there.
	 */
	UPROPERTY(VisibleAnywhere, Category = "UEGT2")
	TObjectPtr<UBoxComponent> Volume;
};
