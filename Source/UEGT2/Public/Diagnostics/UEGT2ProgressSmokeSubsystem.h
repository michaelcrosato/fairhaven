// Fairhaven - opt-in, isolated packaged checkpoint regression.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2ProgressSmokeSubsystem.generated.h"

UCLASS()
class UEGT2_API UUEGT2ProgressSmokeSubsystem : public UTickableWorldSubsystem
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
	enum class EStep : uint8 { Startup, MainImage, PauseImage, SettingsImage, LiveClock, WaitingForTravel };
	void RunPhase();
	void LoadAndCheck();
	void BeginClockCheck();
	void CheckClock();
	void BeginCapture(EStep NextStep, const TCHAR* FileName);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const FString& Reason);
	void RestorePreference();

	FString Phase;
	FString Slot;
	FString CaptureDirectory;
	FString CaptureFile;
	FString PendingFile;
	FDelegateHandle ScreenshotHandle;
	EStep Step = EStep::Startup;
	double StartedSeconds = 0.0;
	double StepStartedSeconds = 0.0;
	float ClockElapsed = 0.0f;
	float LargestClockFrame = 0.0f;
	float ClockStartHour = 0.0f;
	FUEGT2NPCNeeds ClockNeeds;
	FUEGT2Purse ClockPurse;
	bool bRequested = false;
	bool bFinished = false;
	bool bPreferenceChanged = false;
	bool bOriginalPreference = true;
	bool bScreenshotRequested = false;
	bool bScreenshotComplete = false;
	bool bNewVisitWorld = false;
};
