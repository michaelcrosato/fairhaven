// Fairhaven (UEGT2) - one inhabitant. A person or an animal; the difference is
// the routine and the mesh, not the class.
//
// Deliberately animation-free, like the player pawn: there is no skeletal mesh
// and no anim blueprint anywhere in this project. A walking figure is a static
// mesh with a bob, a sway and a lean driven from distance travelled, which at
// this art scale reads as walking and costs a handful of float operations.
//
// Movement follows the baked route network (see UEGT2RouteNetwork), so the
// common case needs no line traces: the node heights come out of the terrain at
// bake time. Only Near-tier NPCs trace, twice a second, to sit exactly on
// uneven ground where the player can see their feet.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/UEGT2Interactable.h"
#include "NPC/UEGT2Dialogue.h"
#include "NPC/UEGT2NPCTypes.h"
#include "UEGT2NPCActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class AUEGT2RouteNetwork;

/** One named place this NPC knows about, baked by the content build. */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2NPCAnchorPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC")
	EUEGT2Anchor Type = EUEGT2Anchor::Home;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC")
	FVector Location = FVector::ZeroVector;
};

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2NPCActor : public AActor, public IUEGT2Interactable
{
	GENERATED_BODY()

public:
	AUEGT2NPCActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Authoring (called from the content build) -------------------------
	/** Identity. Seed drives personality, speech choice and every jitter. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC")
	void ConfigureNPC(const FString& InDisplayName, EUEGT2NPCRole InRole,
		EUEGT2NPCSpecies InSpecies, int32 InSeed);

	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC")
	void SetNPCMesh(UStaticMesh* Mesh);

	/** Register a place. Re-registering a type replaces it. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC")
	void AddAnchor(EUEGT2Anchor Type, const FVector& Location);

	/** How far this NPC drifts about once it has arrived somewhere. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC")
	void SetWanderRadius(float Radius);

	/** Walk speed in cm/s before personality and activity scaling. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC")
	void SetBaseSpeed(float Speed);

	// ---- IUEGT2Interactable ------------------------------------------------
	virtual FText GetInteractionPrompt(const AActor* Interactor) const override;
	virtual bool CanInteract(const AActor* Interactor) const override;
	virtual void Interact(AActor* Interactor) override;
	virtual void SetInteractionFocus(bool bFocused) override;
	virtual FVector GetInteractionPoint() const override;

	// ---- Conversation ------------------------------------------------------
	/** Everything the dialogue is allowed to know: a snapshot, not a pointer. */
	FUEGT2DialogueState MakeDialogueState() const;

	/**
	 * Walk with this actor until told otherwise. Null stops following.
	 *
	 * Following overrides the schedule rather than replacing it: needs still run
	 * down, and somebody who is starving will still break off to eat. That is
	 * the point - a companion who ignores their own hunger is a prop.
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC")
	void SetFollowTarget(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC")
	bool IsFollowing() const { return FollowTarget.IsValid(); }

	/** Say a line as a speech bubble. Used by the conversation UI. */
	void SayReply(const FText& Line);

private:
	void AdvanceFollowing(float DeltaSeconds);

public:

	// ---- Life --------------------------------------------------------------
	/** Re-decide what to do now. Called by the director, and on arrival. */
	void EvaluateSchedule(const FUEGT2NPCContext& Context, bool bForceRepath);

	/**
	 * True once per activity change, then cleared. The director uses it to
	 * decide who has something new worth announcing, without keeping a shadow
	 * copy of everyone's last activity.
	 */
	bool ConsumeActivityChanged();

	/** Put this NPC wherever its routine says it should be, instantly. */
	void SnapToSchedule(const FUEGT2NPCContext& Context);

	/** Advance needs by a slice of world time. */
	void AdvanceNeeds(float WorldHours);

	void SetLOD(EUEGT2NPCLOD NewLOD);
	EUEGT2NPCLOD GetLOD() const { return LOD; }

	/** Hide and stop entirely: the crowd density setting, and far distance. */
	void SetSuppressed(bool bSuppressed);
	bool IsSuppressed() const { return bSuppressed; }

	// ---- Speech ------------------------------------------------------------
	/**
	 * Put a line over this NPC's head.
	 *
	 * TypingSeconds fakes the pause before a message arrives: the bubble shows
	 * animated dots first, then the words. It is the single cheapest thing in
	 * the whole system and it is most of why the bubbles read as messages.
	 */
	void Say(const FText& Line, float HoldSeconds, float TypingSeconds);

	bool HasBubble() const;
	bool IsTyping() const;
	const FText& GetSpokenLine() const { return SpokenLine; }

	/** 0..1 fade, so bubbles arrive and leave rather than blinking. */
	float GetBubbleAlpha() const;

	/** World point the bubble tail should hang from. */
	FVector GetSpeechAnchor() const;

	/** Seconds since this NPC last said anything. Large when it never has. */
	float GetSecondsSinceSpoke() const;

	// ---- Queries -----------------------------------------------------------
	EUEGT2Activity GetActivity() const { return Decision.Activity; }
	EUEGT2ActivityReason GetActivityReason() const { return Decision.Reason; }
	EUEGT2Anchor GetTargetAnchor() const { return Decision.Anchor; }
	EUEGT2NPCRole GetNPCRole() const { return NPCRole; }
	EUEGT2NPCSpecies GetSpecies() const { return Species; }
	const FUEGT2Personality& GetPersonality() const { return Personality; }
	const FUEGT2NPCNeeds& GetNeeds() const { return Needs; }
	int32 GetSeed() const { return Seed; }
	FText GetDisplayName() const { return DisplayName; }
	bool IsAnimal() const { return IsAnimalSpecies(Species); }
	bool IsWalking() const;
	bool IsIndoors() const { return bIndoors; }

	/** Where this NPC is heading, for the dev overlay. */
	FVector GetDestination() const { return Destination; }

	/** Anchor position, falling back to Home and then to the spawn point. */
	FVector GetAnchorLocation(EUEGT2Anchor Type) const;

	/** How many places this inhabitant was given. Zero means a broken bake. */
	int32 GetAnchorCount() const { return Anchors.Num(); }

	UStaticMeshComponent* GetBody() const { return Body; }

	/** Tint for this NPC's bubble: people warm, animals cool. */
	FLinearColor GetBubbleTint() const;

	// ---- Tuning ------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC")
	float BaseSpeed = 155.0f;

	/** How close counts as arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC")
	float ArriveRadius = 160.0f;

	/** Drift radius around the current anchor once arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|NPC")
	float WanderRadius = 500.0f;

protected:
	void RepathTo(const FVector& Goal);
	/** Record where the current leg starts, for the height interpolation. */
	void BeginSegment();
	void AdvanceMovement(float DeltaSeconds);
	void AdvanceCosmetics(float DeltaSeconds);
	void UpdateGroundHeight();

	/**
	 * The height of the walkable surface at Point's XY, near Point's Z.
	 *
	 * Two stages, and both are needed. The first traces from knee height above
	 * Point downward, which finds the floor and cannot find a roof - a market
	 * awning is 2.5 m up and a stall counter 1.5 m, and a trace that starts
	 * above those lands the villager on top of one. The second only runs when
	 * the first found nothing, which means Point is buried inside a hillside:
	 * it drops from well overhead and takes the *lowest* hit, which is the
	 * terrain under whatever else is there.
	 */
	float GroundZAt(const FVector& Point) const;
	void PickArrivalTarget();
	void ApplyIndoors(bool bNewIndoors);
	FVector ResolveDestinationFor(const FUEGT2ActivityDecision& InDecision) const;

	/**
	 * A plain scene root, with the mesh hanging off it.
	 *
	 * This is load bearing. The walk cycle is a relative offset applied to the
	 * mesh every frame, and if the mesh IS the root then "relative" is relative
	 * to the world - so the first cosmetic tick teleports the whole actor to
	 * the world origin. Every NPC within LOD range vanished into the ground
	 * under the town square, which sits at (0, 0), and the only ones left
	 * standing were the ones too far away to be animated.
	 */
	UPROPERTY(VisibleAnywhere, Category = "UEGT2")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "UEGT2")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|NPC")
	EUEGT2NPCRole NPCRole = EUEGT2NPCRole::Villager;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|NPC")
	EUEGT2NPCSpecies Species = EUEGT2NPCSpecies::Person;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|NPC")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|NPC")
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UEGT2|NPC")
	TArray<FUEGT2NPCAnchorPoint> Anchors;

private:
	/** Cached because the personality is a pure function of the seed. */
	FUEGT2Personality Personality;
	FUEGT2NPCNeeds Needs;

	/** Who this NPC is walking with, if anyone. */
	TWeakObjectPtr<AActor> FollowTarget;

	/** Seconds until the next repath toward whoever they are following. */
	float FollowRepathCountdown = 0.0f;
	FUEGT2ActivityDecision Decision;

	FVector SpawnLocation = FVector::ZeroVector;
	FVector Destination = FVector::ZeroVector;
	TArray<FVector> PathPoints;
	int32 PathIndex = 0;

	/** Ground Z the body is standing on, interpolated along the current leg. */
	float GroundZ = 0.0f;

	/**
	 * Where the current leg started, so height can be interpolated by *progress
	 * along it* rather than by a time constant.
	 *
	 * Easing toward the next waypoint's height lags behind on a slope, and at
	 * the start of a leg - where the next waypoint can be fifteen metres and a
	 * couple of metres of hill away - the lag is a visibly airborne villager
	 * for about a second. Lerping by distance covered makes the walk follow the
	 * ground exactly, and costs a subtraction.
	 */
	float SegmentStartZ = 0.0f;
	float SegmentLength = 0.0f;
	/** Seconds until the next ground correction. Near and Mid tiers only. */
	float GroundTraceCountdown = 0.0f;

	/** Distance walked, in centimetres; drives the whole walk cycle. */
	float StridePhase = 0.0f;
	float CurrentYaw = 0.0f;
	float SpeedFraction = 0.0f;

	/** Something to look at while standing still. */
	FVector FocusPoint = FVector::ZeroVector;
	float FocusCountdown = 0.0f;

	/** Seconds until an arrived NPC drifts to a new spot near its anchor. */
	float DriftCountdown = 0.0f;

	/** Kept so an arrived NPC drifts around the anchor, not around itself. */
	FVector AnchorCentre = FVector::ZeroVector;

	/** Progress toward giving up on a blocked route. */
	float StuckSeconds = 0.0f;
	FVector LastStuckCheckLocation = FVector::ZeroVector;

	EUEGT2NPCLOD LOD = EUEGT2NPCLOD::Dormant;
	bool bActivityChanged = false;
	bool bSuppressed = false;
	bool bIndoors = false;
	bool bFocused = false;
	bool bArrived = false;

	// Speech
	FText SpokenLine;
	float BubbleStartTime = -1000.0f;
	float BubbleTypingSeconds = 0.0f;
	float BubbleHoldSeconds = 0.0f;
	float LastSpokeTime = -1000.0f;

	UPROPERTY(Transient) TObjectPtr<AUEGT2RouteNetwork> Routes = nullptr;
};
