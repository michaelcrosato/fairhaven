// Fairhaven - isolated packaged periodic checkpoint regression.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2AutosaveSmokeSubsystem.generated.h"

class SWidget;
class UUEGT2ProgressSave;

struct FUEGT2AutosaveSmokeFile
{
	FString Slot;
	TArray<uint8> Bytes;
	bool bExists = false;
};

UCLASS()
class UEGT2_API UUEGT2AutosaveSmokeSubsystem : public UTickableWorldSubsystem
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
		Startup, FirstWrite, PauseHold, MainHold, SecondWrite, ReadAvailability, ReadLayout,
		MainImage, AutoFocus, SettingsImage, PlayerOff, HardOff, ProgressOff
	};
	void StartPhase();
	void Advance();
	bool SeedPlayer(int32 Fixture);
	bool ReadLatestAuto(int64 Sequence, int32 Fixture, FString& OutSlot);
	void CaptureFiles(bool bIncludeAuto, TArray<FUEGT2AutosaveSmokeFile>& Out) const;
	bool CheckFiles(const TArray<FUEGT2AutosaveSmokeFile>& Baseline, const TCHAR* Reason);
	bool CheckLoadedState();
	bool BeginReadMenu();
	bool CheckMainRow(bool bVisible);
	void LogMainRow(const TCHAR* Context) const;
	bool SendGamepadKey(FKey Key);
	void SetStep(EStep NextStep);
	void BeginCapture(EStep NextStep, const TCHAR* FileName);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void RestorePreferences();

	UPROPERTY(Transient) TObjectPtr<UUEGT2ProgressSave> ExpectedSave = nullptr;
	TArray<FUEGT2AutosaveSmokeFile> ManualFiles;
	TArray<FUEGT2AutosaveSmokeFile> AllFiles;
	TWeakPtr<SWidget> MenuRoot;
	TWeakPtr<SWidget> InitialFocus;
	TWeakPtr<SWidget> AutoButton;
	EStep Step = EStep::Startup;
	FString Phase;
	FString Slot;
	FString RunId;
	FString CaptureDirectory;
	FString CaptureFile;
	FString PendingFile;
	FDelegateHandle ScreenshotHandle;
	double StartedSeconds = 0.0;
	double StepStartedSeconds = 0.0;
	double LastTickSeconds = 0.0;
	double BusyStartedSeconds = 0.0;
	double WorstLiveFrameMs = 0.0;
	uint64 GenerationBefore = 0;
	uint64 WritesBefore = 0;
	int32 ExpectedWidth = 1920;
	int32 ExpectedHeight = 1080;
	bool bRequested = false;
	bool bFinished = false;
	bool bPreferencesChanged = false;
	bool bOriginalSavePreference = true;
	bool bOriginalAutoPreference = false;
	bool bOriginalFeatureEnabled = true;
	bool bOriginalClockEnabled = true;
	bool bScreenshotRequested = false;
	bool bScreenshotComplete = false;
};
