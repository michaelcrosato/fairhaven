// Fairhaven - real contract interactions and isolated checkpoint regression.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2ContractSmokeSubsystem.generated.h"

class AUEGT2InteractableActor;
class AUEGT2Landmark;
class AUEGT2SurveyContract;
class SWidget;

UCLASS()
class UEGT2_API UUEGT2ContractSmokeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bRequested && !bFinished; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;
private:
	enum class EStep : uint8
	{
		Startup, BoardReady, BoardImage, FirstPanel, IncompleteImage, ResumeFirst, Marker,
		ReturnBoard, EligiblePanel, EligibleImage, ClaimFocus, Claimed, PaidImage, SettingsReady, SettingsImage,
		ReadBoard, ReadPanel, DisabledBoard, HardDisabledBoard, WaitingForTravel
	};
	void StartCheck();
	void Advance();
	void SetStep(EStep Next);
	bool Check(bool Condition, const TCHAR* Reason);
	void Finish(bool Success, const TCHAR* Reason);
	void RestorePreferences();
	bool PositionAt(AUEGT2InteractableActor* Target);
	bool Probe(AUEGT2InteractableActor* Target);
	bool CheckState(float Coins, bool Paid, int32 Discoveries);
	bool SlateKey(FKey Key);
	TSharedPtr<SWidget> FindButton(const FText& Caption) const;
	bool HasFocus(const FText& Caption) const;
	void BeginCapture(EStep Next, const TCHAR* Name);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	EStep Step = EStep::Startup;
	FString Phase, Slot, RunId, CaptureDirectory, CaptureFile, PendingFile;
	FDelegateHandle ScreenshotHandle;
	TWeakObjectPtr<AUEGT2SurveyContract> Board;
	TArray<TWeakObjectPtr<AUEGT2Landmark>> Targets;
	int32 MarkerIndex = 0, ApproachIndex = 0, ProbeAttempts = 0;
	int32 ExpectedWidth = 1920, ExpectedHeight = 1080;
	double StartedSeconds = 0.0, StepStartedSeconds = 0.0;
	bool bRequested = false, bFinished = false, bChanged = false, bAfterTravel = false;
	bool bOriginalSave = true, bOriginalAutosave = false, bOriginalContract = true, bOriginalGate = true, bOriginalClock = true;
	bool bScreenshotRequested = false, bScreenshotComplete = false;
};
