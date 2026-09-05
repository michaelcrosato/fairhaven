// Fairhaven - opt-in packaged bed and chosen-hour rest regression.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "NPC/UEGT2NPCTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2RestSmokeSubsystem.generated.h"

class AUEGT2Amenity;
class AUEGT2NPCActor;
class SWidget;

struct FUEGT2RestSmokeNPCState
{
	TWeakObjectPtr<AUEGT2NPCActor> Actor;
	FUEGT2NPCNeeds Needs;
	FUEGT2Purse Purse;
};

UCLASS()
class UEGT2_API UUEGT2RestSmokeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bRequested && !bFinished; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

private:
	enum class EStep : uint8
	{
		Startup, Approach, CancelReady, Cancel, Reopen, PreparePanel,
		EarlierFocus, LaterFocus, FirstHour, SecondHour, SleepFocus, PanelImage, AwakeImage, DebtCheck,
		LiveClock, PlayerOff, PlayerSleeping, HardOff, HardSleeping
	};
	void StartCheck();
	void Advance();
	bool PositionAtBed();
	bool UseBed();
	bool CheckFocusedButton(const FText& Caption);
	bool CheckVisibleHour(int32 Hour);
	bool SendGamepadKey(FKey Key);
	bool CapturePopulation(TArray<FUEGT2RestSmokeNPCState>& Out);
	bool CheckWakeSnapshot();
	void SetStep(EStep NextStep);
	void BeginCapture(EStep NextStep, const TCHAR* FileName);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void RestorePreferences();

	EStep Step = EStep::Startup;
	TWeakObjectPtr<AUEGT2Amenity> Bed;
	TWeakPtr<SWidget> PanelRoot;
	TArray<FUEGT2RestSmokeNPCState> BeforePopulation;
	TArray<FUEGT2RestSmokeNPCState> WakePopulation;
	FUEGT2NPCNeeds ExpectedNeeds;
	FUEGT2Purse ExpectedPurse;
	FUEGT2NPCNeeds SnapshotNeeds;
	FUEGT2Purse SnapshotPurse;
	FString RunId;
	FString CaptureDirectory;
	FString CaptureFile;
	FString PendingFile;
	FDelegateHandle ScreenshotHandle;
	double StartedSeconds = 0.0;
	double StepStartedSeconds = 0.0;
	double WakeStartedSeconds = 0.0;
	double SkipMilliseconds = 0.0;
	float SnapshotHour = 0.0f;
	float LiveElapsed = 0.0f;
	float LargestLiveFrame = 0.0f;
	float OriginalDayLength = 20.0f;
	float OriginalDensity = 1.0f;
	int32 SnapshotDay = 0;
	int32 NextApproach = 0;
	int32 ProbeAttempts = 0;
	int32 ExpectedWidth = 1920;
	int32 ExpectedHeight = 1080;
	bool bRequested = false;
	bool bFinished = false;
	bool bPreferencesChanged = false;
	bool bOriginalSavePreference = true;
	bool bOriginalRestPreference = true;
	bool bOriginalAutoWalkPreference = false;
	bool bOriginalAutoWalkGate = true;
	bool bOriginalFeatureEnabled = true;
	bool bOriginalClockEnabled = true;
	bool bScreenshotRequested = false;
	bool bScreenshotComplete = false;
};
