// Fairhaven - isolated checks of the real HUD reminder timer and off paths.
#pragma once

#include "CoreMinimal.h"
#include "NPC/UEGT2NPCTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2NeedsRemindersSmokeSubsystem.generated.h"

UCLASS()
class UEGT2_API UUEGT2NeedsRemindersSmokeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static bool IsRequested();
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bRequested && !bFinished; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;
private:
	enum class EStep : uint8 { Startup, Busy, FirstReminder, PlayerOff, Reenabled,
		Interrupted, Restored, HardOff, Settings, SettingsImage, Done };
	bool Check(bool bCondition, const TCHAR* Reason);
	bool CheckLife();
	bool CheckImageState();
	void StartCheck();
	void Advance();
	void SetStep(EStep Next);
	void Capture(const TCHAR* Name, EStep Next);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	bool CheckSettings(bool bScroll);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void RestorePreferences();
	EStep Step = EStep::Startup;
	EStep AfterImage = EStep::Startup;
	FUEGT2NPCNeeds ExpectedNeeds;
	FString RunId, CaptureDirectory, PendingImage;
	FDelegateHandle ScreenshotHandle;
	double Started = 0, StepStarted = 0, ImageStarted = 0, PhaseWorldTime = 0;
	int32 ExpectedWidth = 1920, ExpectedHeight = 1080;
	int32 OriginalHudSize = 0;
	bool bOriginalPlayerGate = true, bOriginalHardGate = true, bOriginalCdoGate = true;
	bool bOriginalNeeds = true, bOriginalSpeech = true, bOriginalClock = true;
	bool bOriginalHudScale = true, bOriginalServices = true;
	bool bRequested = false, bFinished = false, bChanged = false;
	bool bImagePending = false, bImageRequested = false, bImageComplete = false;
};
