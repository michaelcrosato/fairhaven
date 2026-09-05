// Fairhaven - opt-in packaged survey journal regression.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2SurveySmokeSubsystem.generated.h"

UCLASS()
class UEGT2_API UUEGT2SurveySmokeSubsystem : public UTickableWorldSubsystem
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
		Startup, MappingsReady, OldKey, OpenEmpty, EmptyImage, OpenSurveyed,
		SurveyedImage, TrackingImage, PauseImage, SettingsImage,
		EnterMappings, OpenEnter, FocusedEnter, PlayerOff, HardOff
	};
	void StartCheck();
	void Advance();
	void SetStep(EStep NextStep);
	void BeginCapture(EStep NextStep, const TCHAR* FileName);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	bool CloseWithSlateKey(FKey Key = EKeys::K);
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void RestorePreferences();

	EStep Step = EStep::Startup;
	FString RunId;
	FString CaptureDirectory;
	FString CaptureFile;
	FString PendingFile;
	FDelegateHandle ScreenshotHandle;
	FKey OriginalJournalKey;
	double StartedSeconds = 0.0;
	double StepStartedSeconds = 0.0;
	int32 ExpectedWidth = 1920;
	int32 ExpectedHeight = 1080;
	bool bRequested = false;
	bool bFinished = false;
	bool bPreferencesChanged = false;
	bool bOriginalSavePreference = true;
	bool bOriginalSurveyPreference = true;
	bool bOriginalFeatureEnabled = true;
	bool bScreenshotRequested = false;
	bool bScreenshotComplete = false;
};
