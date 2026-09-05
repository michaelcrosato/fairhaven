// Fairhaven (UEGT2) - the thing that makes a few hundred NPCs affordable, and
// the thing that decides who speaks.
//
// Every NPC could work out the hour, find the player and decide whether to open
// its mouth on its own. Six hundred of them doing that is six hundred copies of
// the same work and a town where everybody talks at once. So the director owns
// the shared facts (hour, weather, where the player is), hands out simulation
// tiers by distance, walks the population in slices rather than all at once,
// and rations speech against a global budget.
//
// A world subsystem rather than an actor, for the same reason as dev mode:
// nothing here belongs in the map, so a content rebuild cannot destroy it.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/UEGT2Weather.h"
#include "UEGT2NPCDirector.generated.h"

class AUEGT2NPCActor;
class AUEGT2SkyController;

/** One bubble, ready to draw. Produced by the director, consumed by the HUD. */
struct FUEGT2SpeechBubble
{
	FVector WorldLocation = FVector::ZeroVector;
	FText Speaker;
	FText Line;
	FLinearColor Tint = FLinearColor::White;
	float Alpha = 1.0f;
	float Distance = 0.0f;
	bool bTyping = false;
	bool bAnimal = false;
};

/** A two-line exchange between two NPCs standing near each other. */
USTRUCT()
struct FUEGT2Conversation
{
	GENERATED_BODY()

	UPROPERTY() TWeakObjectPtr<AUEGT2NPCActor> Opener;
	UPROPERTY() TWeakObjectPtr<AUEGT2NPCActor> Responder;
	float StartTime = 0.0f;
	bool bReplied = false;
};

UCLASS()
class UEGT2_API UUEGT2NPCDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---- Subsystem ---------------------------------------------------------
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	static UUEGT2NPCDirector* Get(const UWorld* World);

	// ---- Registry ----------------------------------------------------------
	void RegisterNPC(AUEGT2NPCActor* NPC);
	void UnregisterNPC(AUEGT2NPCActor* NPC);

	// ---- Shared world facts ------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") float GetHour() const { return Hour; }
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") EUEGT2Weather GetWeather() const { return Weather; }
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") int32 GetDayIndex() const { return DayIndex; }
	FVector GetPlayerLocation() const { return PlayerLocation; }

	/**
	 * World hours per real second, at the current day length. Zero when the
	 * clock is frozen.
	 *
	 * The player's needs run off this rather than off their own conversion, so
	 * a day that is twenty minutes long makes the player hungry exactly as
	 * often as it makes the town hungry.
	 */
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") float GetWorldHoursPerSecond() const;

	/** "market day", "rest day" or an empty string. For the dev readout. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") FText GetDayLabel() const;

	// ---- Counts, for the overlay and the tests -----------------------------
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") int32 GetPopulation() const { return Population.Num(); }
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") int32 GetPeopleCount() const;
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") int32 GetAnimalCount() const;
	/** How many are outdoors and simulated rather than asleep behind a door. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") int32 GetActiveCount() const;
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") int32 GetNearCount() const { return NearCount; }
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") int32 GetSpeakingCount() const;

	// ---- Speech ------------------------------------------------------------
	/** Bubbles worth drawing this frame, nearest last so they paint on top. */
	void GatherBubbles(const FVector& ViewLocation, TArray<FUEGT2SpeechBubble>& OutBubbles) const;

	/** Make everyone nearby announce what they are up to. Dev mode and console. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC") int32 TriggerChatter();

	// ---- Controls ----------------------------------------------------------
	/** Stop re-deciding activities. NPCs finish walking where they were going. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC") void SetSchedulesPaused(bool bPaused);
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") bool AreSchedulesPaused() const { return bSchedulesPaused; }

	/** Draw each NPC's activity and destination in the world. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC") void SetDebugOverlay(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") bool IsDebugOverlay() const { return bDebugOverlay; }

	/** 0.1 - 1.0. Hides a deterministic slice of the population. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|NPC") void SetCrowdDensity(float Density);
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") float GetCrowdDensity() const { return CrowdDensity; }

	/** True during a screenshot tour or the packaged walk smoke. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|NPC") bool IsFrozen() const { return bFrozen; }

	/** -UEGT2LiveNPCs: log every line, and every bubble the HUD lays out. */
	bool IsLoggingSpeech() const { return bLogSpeech; }

	// ---- Tuning ------------------------------------------------------------
	/** Past this the player cannot read a bubble anyway. */
	static constexpr float BubbleRadius = 4200.0f;
	static constexpr float NearRadius = 6500.0f;
	static constexpr float MidRadius = 16000.0f;
	static constexpr float FarRadius = 42000.0f;
	static constexpr int32 MaxConcurrentBubbles = 5;

private:
	void RefreshWorldFacts();
	void UpdateLODs();
	void RunScheduleSlice();
	void AdvanceNeedsToNow(AUEGT2NPCActor* NPC);
	void UpdateSpeech();
	void UpdateConversations();
	void DrawDebug() const;
	void SnapEveryone();
	/** One line, a few seconds in, saying whether the town is actually alive. */
	void LogPopulationReport();
	void ApplyCrowdDensity();
	void RegisterConsoleCommands();
	void RefreshFromSettings();

	FUEGT2NPCContext MakeContext() const;
	bool CanSpeakNow(const AUEGT2NPCActor* NPC, float MinCooldown) const;
	void SpeakAnnounce(AUEGT2NPCActor* NPC, EUEGT2SpeechMood Mood, uint32 Variation = 0u);

	UPROPERTY(Transient) TArray<TObjectPtr<AUEGT2NPCActor>> Population;
	UPROPERTY(Transient) TObjectPtr<AUEGT2SkyController> Sky = nullptr;
	/** Population owns these actors; registry changes keep their clocks in step. */
	TMap<AUEGT2NPCActor*, double> LastNeedsHours;

	TArray<FUEGT2Conversation> Conversations;

	float Hour = 10.5f;
	float PreviousHour = 10.5f;
	int32 DayIndex = 0;
	EUEGT2Weather Weather = EUEGT2Weather::Clear;
	EUEGT2Weather PreviousWeather = EUEGT2Weather::Clear;

	FVector PlayerLocation = FVector::ZeroVector;
	bool bHasPlayer = false;

	float LodCountdown = 0.0f;
	float ScheduleCountdown = 0.0f;
	float SpeechCountdown = 0.0f;
	float LastSpeechTime = -100.0f;
	/** Integrated live world time, independent of slice size and frame hitches. */
	double SimulatedHours = 0.0;
	/** Counts down to the one-shot population report. Negative once spent. */
	float ReportCountdown = -1.0f;
	/** Frames and seconds accumulated over the report window, for a mean fps. */
	int32 ReportFrames = 0;
	float ReportSeconds = 0.0f;

	/** Which slice of the population the next schedule pass covers. */
	int32 SliceCursor = 0;

	int32 NearCount = 0;
	bool bSchedulesPaused = false;
	bool bDebugOverlay = false;
	bool bFrozen = false;
	bool bSnapped = false;
	/**
	 * Log every line spoken. Set by -UEGT2LiveNPCs, and only by that.
	 *
	 * A screenshot cannot tell you the difference between "nobody happened to
	 * be talking in that frame" and "the bubbles do not draw", and finding that
	 * out by repackaging with a temporary log is a five minute round trip.
	 */
	bool bLogSpeech = false;
	float CrowdDensity = 1.0f;

	/** Population is walked in this many slices, one per schedule pass. */
	static constexpr int32 ScheduleSlices = 6;
	static constexpr float SchedulePeriod = 0.25f;
	static constexpr float LodPeriod = 0.35f;
	static constexpr float SpeechPeriod = 0.45f;
	/** Nothing speaks within this of the last bubble anywhere. */
	static constexpr float GlobalSpeechGap = 1.1f;
};
