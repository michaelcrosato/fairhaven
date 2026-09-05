// Fairhaven - bounded packaged auto-walk input and cancellation regression.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2AutoWalkSmokeSubsystem.generated.h"

UCLASS()
class UEGT2_API UUEGT2AutoWalkSmokeSubsystem : public UTickableWorldSubsystem
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
		Startup, Ready, OldKey, Forward, Steering, Manual, NormalReady, NormalStart,
		NormalImage, GamepadStop, MenuReady, MenuStart, MenuPaused, MenuClosed, MenuHeld,
		FocusReady, FocusStart, FocusFlushed, FocusHeld, ConsoleReady, ConsoleStart, ConsoleOpen,
		ConsoleClosed, ConsoleHeld, LargerReady, LargerStart, LargerImage,
		PreferenceStopped, PreferenceManual, PreferenceOn, HardStart, HardStopped, HardManual,
		HardOn, SettingsReady, SettingsImage
	};
	void StartCheck();
	void Advance();
	bool FindClearRoute();
	bool ResetPosition();
	bool CheckWalking(bool bExpected, const TCHAR* Reason);
	bool CheckDisplacement(const FVector& Direction, float Minimum, const TCHAR* Reason);
	bool CheckIndicator();
	bool CheckSettings();
	void Key(FKey Key, EInputEvent Event);
	bool ConsoleKey(FKey Key);
	void SetStep(EStep Next);
	void BeginCapture(EStep Next, const TCHAR* Name);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void RestorePreferences();

	EStep Step = EStep::Startup;
	FVector RouteStart = FVector::ZeroVector;
	FVector StepLocation = FVector::ZeroVector;
	FRotator RouteRotation = FRotator::ZeroRotator;
	FString RunId;
	FString CaptureDirectory;
	FString CaptureFile;
	FString PendingFile;
	FDelegateHandle ScreenshotHandle;
	FKey OriginalKey;
	double StartedSeconds = 0.0;
	double StepStartedSeconds = 0.0;
	int32 ExpectedWidth = 1920;
	int32 ExpectedHeight = 1080;
	int32 OriginalHudSize = 0;
	bool bOriginalPreference = false;
	bool bOriginalHardGate = true;
	bool bOriginalSave = true;
	bool bOriginalAutosave = false;
	bool bOriginalNeeds = true;
	bool bOriginalClock = true;
	bool bOriginalHudGate = true;
	bool bRequested = false;
	bool bFinished = false;
	bool bPreferencesChanged = false;
	bool bScreenshotRequested = false;
	bool bScreenshotComplete = false;
};
