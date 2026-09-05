// Fairhaven (UEGT2) - the player's four needs, their trade and their purse.
//
// This is the whole of "the player is an inhabitant too". It holds the same
// FUEGT2NPCNeeds every villager has, the same FUEGT2Purse, and it advances
// both through UEGT2AdvanceLife - the same function AUEGT2NPCActor calls. None
// of the numbers are duplicated here, on purpose: if eating stops costing five
// an hour it stops costing five an hour for everybody at once.
//
// What is specific to the player is *how* an activity starts. An NPC decides
// by routine; the player decides by walking up to a bench and pressing use.
// AUEGT2Amenity is the other half of that, and it does nothing except call
// BeginActivity here.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPC/UEGT2NPCTypes.h"
#include "UEGT2NeedsComponent.generated.h"

/** Fired when the player starts or stops doing something. Text is a reason. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FUEGT2PlayerActivityChanged,
	EUEGT2Activity /*Activity*/, const FText& /*Note*/);

UCLASS(ClassGroup = "UEGT2", meta = (BlueprintSpawnableComponent))
class UEGT2_API UUEGT2NeedsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUEGT2NeedsComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Queries -----------------------------------------------------------
	const FUEGT2NPCNeeds& GetNeeds() const { return Needs; }
	const FUEGT2Purse& GetPurse() const { return Purse; }

	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	int32 GetCoins() const { return Purse.Whole(); }

	/** What the player is doing right now, in the town's own vocabulary. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	EUEGT2Activity GetActivity() const { return Activity; }

	/** The trade the player was last hired for. Villager until they take a job. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	EUEGT2NPCRole GetTrade() const { return Trade; }

	/** Where they are doing it, if anywhere: "The Bakehouse". */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	FText GetVenueName() const { return VenueName; }

	/** True while occupied at an amenity, as opposed to just walking about. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	bool IsOccupied() const { return Venue.IsValid(); }

	/** True when this exact place is the one currently being used. */
	bool IsUsing(const AActor* InVenue) const { return InVenue && Venue.Get() == InVenue; }

	/** "eating at The Bakehouse", "working the quay", "out for a walk". */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	FText GetActivityText() const;

	/**
	 * Walk speed scale from how tired the player is: 1 while rested, falling
	 * to WornOutScale on empty.
	 *
	 * Needs the player cannot feel are decoration. An NPC shows you theirs by
	 * walking off to do something about them; the player has no such tell, so
	 * exhaustion is in the legs instead.
	 */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	float GetExertionScale() const;

	/** False once Energy is gone: no sprinting on an empty tank. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Player")
	bool CanSprint() const { return Needs.Energy > SpentEnergy; }

	// ---- Doing things ------------------------------------------------------
	/**
	 * Start a sustained activity at a place, and stay in it until the player
	 * walks off, presses use again, or runs out of money for it.
	 *
	 * JobRole is the trade the venue hires for, and only matters for work: it
	 * is what the wage is paid at. Returns false when the first instant of it
	 * could not be paid for, in which case nothing starts.
	 */
	bool BeginActivity(EUEGT2Activity InActivity, AActor* InVenue, const FText& InVenueName,
		EUEGT2NPCRole JobRole, float InUseRange);

	/** Stop whatever is being used. Note, when set, is shown to the player. */
	void StopActivity(const FText& Note);

	/**
	 * Charge and pay for WorldHours of the current activity.
	 *
	 * The tick works out how much world time has gone by and calls this; the
	 * capture tour calls it with a fixed slice instead, so a screenshot of
	 * somebody eating is the same screenshot every run. Splitting "how much
	 * time" from "what that time costs" is what makes both possible.
	 */
	void AdvanceLife(float WorldHours);

	/** Talking to somebody is company, exactly as it is for them. */
	void SetConversing(bool bTalking);

	/** Exact durable state; validation never clamps a corrupt save into a valid one. */
	static bool IsValidProgress(const FUEGT2NPCNeeds& InNeeds, const FUEGT2Purse& InPurse,
		EUEGT2NPCRole InTrade);
	/** Restore after BeginPlay, idle and unoccupied, without charging for offline time. */
	bool RestoreProgress(const FUEGT2NPCNeeds& InNeeds, const FUEGT2Purse& InPurse,
		EUEGT2NPCRole InTrade);
	/** Transient discontinuity, not elapsed life: lets reminders discard pre-load state. */
	uint64 GetNeedsRevision() const { return NeedsRevision; }
	/** One-off shared-ledger payment. No needs, activity, trade or delegate changes. */
	bool TryCredit(float Amount);

	// ---- Dev mode ----------------------------------------------------------
	void SetCoins(float Amount);
	void SetNeedsSatisfied(bool bFull);

	FUEGT2PlayerActivityChanged OnActivityChanged;

	// ---- Tuning ------------------------------------------------------------
	/** Below this the legs start to go. */
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Needs") float TiredEnergy = 0.35f;
	/** At zero energy, this much of normal walking speed. */
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Needs") float WornOutScale = 0.55f;
	/** Sprinting stops working at or below this. */
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Needs") float SpentEnergy = 0.08f;
	/** A need this full is as good as done; using the place stops by itself. */
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Needs") float SatisfiedAt = 0.985f;
	/** Horizontal speed that counts as walking rather than standing. */
	UPROPERTY(EditDefaultsOnly, Category = "UEGT2|Needs") float MovingSpeed = 40.0f;

private:
	/** What the player is doing when they are not using anything. */
	EUEGT2Activity IdleActivity() const;

	/** Distance and liveness checks on the venue; stops the activity if it fails. */
	void CheckVenueStillValid();

	/** The need this activity answers, or nullptr for the ones that answer none. */
	const float* NeedFor(EUEGT2Activity InActivity) const;

	void Announce(const FText& Message) const;
	void SetActivity(EUEGT2Activity NewActivity, const FText& Note);

	FUEGT2NPCNeeds Needs;
	FUEGT2Purse Purse;
	uint64 NeedsRevision = 0;

	EUEGT2Activity Activity = EUEGT2Activity::Idle;
	EUEGT2NPCRole Trade = EUEGT2NPCRole::Villager;

	TWeakObjectPtr<AActor> Venue;
	FText VenueName;
	EUEGT2Activity VenueActivity = EUEGT2Activity::Idle;
	float UseRange = 400.0f;

	bool bConversing = false;
	/** Logged once, so an empty purse says so rather than silently doing nothing. */
	bool bWarnedBroke = false;
};
