// Fairhaven - opt-in packaged nearby-services regression.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "NPC/UEGT2NPCTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2ServicesSmokeSubsystem.generated.h"

class AUEGT2Amenity;
class AUEGT2Landmark;
class SWidget;

UCLASS()
class UEGT2_API UUEGT2ServicesSmokeSubsystem : public UTickableWorldSubsystem
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
		Startup, Settled, PauseReady, FindGuide, GuideReady, GuideImage, FindHome, Tracked,
		FindResume, Resumed, NormalImage, LargerImage, UseHome,
		UseWork, PlayerOff, PlayerOffUse, HardOff, HardOffUse,
		SettingsReady, SettingsImage
	};
	void StartCheck();
	void Advance();
	void SetStep(EStep Next);
	bool Check(bool Condition, const TCHAR* Reason);
	void Finish(bool Success, const TCHAR* Reason);
	void Restore();
	void Key(FKey Button, EInputEvent Event);
	bool SlateKey(FKey Button);
	TSharedPtr<SWidget> GetMenuRoot() const;
	bool FocusHasCaption(const FText& Caption) const;
	bool FocusIsHomeTrack() const;
	bool ValidateEntries();
	bool CheckLedger() const;
	bool CheckDirection();
	bool PositionAt(AUEGT2Amenity* Amenity);
	bool Use(AUEGT2Amenity* Amenity);
	void BeginCapture(EStep Next, const TCHAR* Name);
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	EStep Step = EStep::Startup;
	FString RunId, CaptureDirectory, CaptureFile, PendingFile;
	FDelegateHandle ScreenshotHandle;
	TWeakObjectPtr<AUEGT2Amenity> Home, Work;
	TWeakObjectPtr<AUEGT2Landmark> Landmark;
	FText HomeCategory;
	FVector HudLocation = FVector::ZeroVector;
	FRotator HudView = FRotator::ZeroRotator;
	FUEGT2NPCNeeds OriginalNeeds, SnapshotNeeds;
	FUEGT2Purse OriginalPurse, SnapshotPurse;
	EUEGT2NPCRole OriginalTrade = EUEGT2NPCRole::Villager;
	EUEGT2NPCRole SnapshotTrade = EUEGT2NPCRole::Villager;
	int32 ExpectedWidth = 1920, ExpectedHeight = 1080, OriginalHudSize = 0;
	int32 SnapshotDay = 0, DiscoveryCount = 0, NavigationSteps = 0, ApproachIndex = 0, ProbeAttempts = 0;
	float SnapshotHour = 0.0f;
	double StartedSeconds = 0.0, StepStartedSeconds = 0.0;
	bool bRequested = false, bFinished = false, bChanged = false;
	bool bOriginalServices = true, bOriginalSave = true, bOriginalAutosave = false;
	bool bOriginalSurvey = true, bOriginalAutoWalk = false, bOriginalNeeds = true, bOriginalPrompts = true;
	bool bOriginalServiceGate = true, bOriginalSurveyGate = true, bOriginalAutoWalkGate = true, bOriginalHudGate = true;
	bool bOriginalClock = true, bOriginalDiscovered = false;
	bool bScreenshotRequested = false, bScreenshotComplete = false;
};
