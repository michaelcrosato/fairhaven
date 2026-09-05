// Fairhaven - isolated same-scene Canvas HUD size regression.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2HudSizeSmokeSubsystem.generated.h"

class AUEGT2NPCActor;

UCLASS()
class UEGT2_API UUEGT2HudSizeSmokeSubsystem : public UTickableWorldSubsystem
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
	enum class EStep : uint8 { Startup, FindSpeaker, Probe, Speech, Paused, HudImage, SettingsReady, SettingsImage };
	void StartCheck();
	void Advance();
	bool PositionAtSpeaker();
	bool CheckFixture();
	bool CheckSettings();
	void SetStep(EStep Next);
	void CaptureHud(int32 Index);
	void BeginCapture(EStep Next, const TCHAR* Name);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void RestorePreferences();

	EStep Step = EStep::Startup;
	TWeakObjectPtr<AUEGT2NPCActor> Speaker;
	FUEGT2NPCNeeds SnapshotNeeds;
	FVector SnapshotPlayer = FVector::ZeroVector;
	FVector SnapshotSpeaker = FVector::ZeroVector;
	FVector SnapshotView = FVector::ZeroVector;
	FRotator SnapshotRotation = FRotator::ZeroRotator;
	FString SnapshotPrompt;
	FString RunId;
	FString CaptureDirectory;
	FString CaptureFile;
	FString PendingFile;
	FDelegateHandle ScreenshotHandle;
	double StartedSeconds = 0.0;
	double StepStartedSeconds = 0.0;
	// UWorld stores seconds as double; preserve it for the exact paused check.
	double SnapshotTime = 0.0;
	int32 ImageIndex = 0;
	int32 ProbeAttempts = 0;
	int32 NextApproach = 0;
	int32 ExpectedWidth = 1920;
	int32 ExpectedHeight = 1080;
	int32 OriginalSize = 0;
	bool OriginalVisible[5] = {};
	bool bOriginalSave = true;
	bool bOriginalAutosave = false;
	bool bOriginalSurvey = true;
	bool bOriginalHardGate = true;
	bool bOriginalClock = true;
	bool bOriginalDiagnostics = false;
	bool bOriginalSpeakerTick = true;
	bool bRequested = false;
	bool bFinished = false;
	bool bPreferencesChanged = false;
	bool bScreenshotRequested = false;
	bool bScreenshotComplete = false;
};
