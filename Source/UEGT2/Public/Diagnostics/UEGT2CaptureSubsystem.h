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
//   -UEGT2SmokeWalk             inject real input and verify the player moves
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2CaptureSubsystem.generated.h"

class ACameraActor;

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

	/** The tour definition. Edit here to change what gets reviewed. */
	static const TArray<FUEGT2Viewpoint>& GetTour();

private:
	void BeginTour();
	void CaptureNext();
	void BeginMenuTour();
	void RunMenuStep();
	void BeginWalkSmoke();
	bool TickWalkSmoke(float DeltaSeconds);
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
	FVector WalkStart = FVector::ZeroVector;
	float WalkElapsed = 0.0f;

	UPROPERTY(Transient) TObjectPtr<ACameraActor> TourCamera = nullptr;
};
