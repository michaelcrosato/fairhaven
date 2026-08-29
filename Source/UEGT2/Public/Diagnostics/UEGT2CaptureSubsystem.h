// Fairhaven (UEGT2) - automated screenshot tours.
//
// Drives the camera to a set of viewpoints and writes a PNG at each one, so the
// look of the world can be reviewed from a headless run. This is how visual
// changes get verified without a human opening the editor.
//
// Usage (see Scripts/Screenshot-Tour.ps1):
//   -UEGT2Capture=<directory>   run the tour and write PNGs there
//   -UEGT2CaptureDelay=<sec>    settle time before the first shot (default 6)
//   -UEGT2CaptureHold=<sec>     settle time at each viewpoint (default 1.6)
//   -UEGT2CaptureOnly=<name>    capture just one named viewpoint
//   -UEGT2CaptureMenu           capture the menu screens instead of the world
//   -UEGT2CaptureLife           walk up to one amenity of each kind and use it
//   -UEGT2SmokeWalk             inject real input and verify the player moves
//   -UEGT2SmokeFly              fly god mode for minutes, logging every hitch
//   -UEGT2SmokeMinutes=<n>      how long the fly soak runs (default 6)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2CaptureSubsystem.generated.h"

class ACameraActor;
class AUEGT2Amenity;

/** One stop on the tour. Position is world XY; Z is metres above the ground. */
USTRUCT()
struct FUEGT2Viewpoint
{
	GENERATED_BODY()

	FName Name;
	FVector2D Location = FVector2D::ZeroVector;
	float HeightAboveGround = 2.0f;
	float Yaw = 0.0f;
	float Pitch = -6.0f;
	FString Description;

	/**
	 * Treat HeightAboveGround as a world Z in centimetres rather than metres
	 * above the ground.
	 *
	 * Interior viewpoints need this. Finding the ground is a downward line
	 * trace, and inside a building the only thing above the camera is the roof,
	 * so the trace lands the camera on the tiles. There is no cheap trace that
	 * finds the floor underneath instead: a multi-trace stops at the first
	 * blocking hit, and the shell is a single component, so it reports the roof
	 * and nothing else.
	 */
	bool bAbsoluteHeight = false;
};

UCLASS()
class UEGT2_API UUEGT2CaptureSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** True when a capture tour was requested on the command line. */
	static bool IsCaptureRequested();

	/** True when the walk smoke test was requested on the command line. */
	static bool IsWalkSmokeRequested();

	/** True when the amenity tour was requested on the command line. */
	static bool IsLifeCaptureRequested();

	/** True when the god-mode fly soak was requested on the command line. */
	static bool IsFlySoakRequested();

	/** The tour definition. Edit here to change what gets reviewed. */
	static const TArray<FUEGT2Viewpoint>& GetTour();

private:
	void BeginTour();
	void CaptureNext();
	void BeginMenuTour();
	void RunMenuStep();
	void BeginDialogueTour();
	void RunDialogueStep();
	/**
	 * Stand at one amenity of each kind, use it, and photograph the result.
	 *
	 * This is the only check that covers the whole path the player actually
	 * takes - probe, prompt, use, activity, purse, HUD - rather than the pieces
	 * of it. UEGT2.Economy proves the arithmetic and UEGT2.Content proves the
	 * amenities were placed; neither can tell you that pressing the key does
	 * anything, and that is exactly the sort of thing that produces a clean log
	 * over a broken game.
	 */
	void BeginLifeTour();
	void RunLifeStep();
	void BeginWalkSmoke();
	bool TickWalkSmoke(float DeltaSeconds);

	/**
	 * Fly around in god mode for several minutes and report every stall.
	 *
	 * Exists because "it freezes after three to five minutes of flying" is not
	 * something a screenshot or a unit test can see. It flies the real pawn
	 * with the real dev-mode flags through the real input path, and every
	 * second it prints the worst frame in that second alongside what was going
	 * on - so a stall comes with its own context instead of a stopwatch and a
	 * guess. Garbage collection is timed separately, because a level with two
	 * thirds of a million components in it is exactly where a full purge shows
	 * up as a freeze.
	 */
	void BeginFlySoak();
	bool TickFlySoak(float DeltaSeconds);
	void ReportFlySecond();
	void FinishTour();
	void HandleScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	float GroundHeightAt(float WorldX, float WorldY) const;

	TArray<FUEGT2Viewpoint> Tour;
	int32 Index = INDEX_NONE;
	FString OutputDirectory;
	float HoldSeconds = 1.6f;
	FTimerHandle TimerHandle;
	FString PendingFileName;
	FDelegateHandle ScreenshotHandle;
	bool bMenuMode = false;
	int32 MenuIndex = 0;
	bool bDialogueMode = false;
	int32 DialogueIndex = 0;
	TWeakObjectPtr<class AUEGT2NPCActor> DialoguePartner;
	bool bLifeMode = false;
	int32 LifeIndex = 0;
	UPROPERTY(Transient) TArray<TObjectPtr<AUEGT2Amenity>> LifeStops;
	FVector WalkStart = FVector::ZeroVector;
	float WalkElapsed = 0.0f;

	// --- fly soak ----------------------------------------------------------
	/** Where the circuit goes: the inhabited viewpoints, in order. */
	TArray<FVector2D> FlyStops;
	int32 FlyStop = 0;
	int32 FlyLaps = 0;
	float FlyAltitude = 1800.0f;
	float FlyElapsed = 0.0f;
	float FlyLimitSeconds = 360.0f;
	float FlySecondElapsed = 0.0f;
	int32 FlySecondFrames = 0;
	float FlySecondWorst = 0.0f;
	float FlyWorstEver = 0.0f;
	int32 FlyStalls = 0;
	/** Set by the pre-GC hook, read and cleared by the report. */
	double FlyGcStartSeconds = 0.0;
	float FlyGcLastMs = 0.0f;
	float FlyGcWorstMs = 0.0f;
	int32 FlyGcCount = 0;
	float FlyStuckSeconds = 0.0f;
	FVector FlyLastCheck = FVector::ZeroVector;
	FDelegateHandle FlyGcPreHandle;
	FDelegateHandle FlyGcPostHandle;

	UPROPERTY(Transient) TObjectPtr<ACameraActor> TourCamera = nullptr;
};
