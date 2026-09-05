// Opt-in observation of the complete ordinary Town Survey Contract walk.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "NPC/UEGT2NPCTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/UEGT2Weather.h"
#include "UEGT2ContractWalkSmokeSubsystem.generated.h"

class AStaticMeshActor;
class AUEGT2Character;
class AUEGT2InteractableActor;
class AUEGT2Landmark;
class AUEGT2PlayerController;
class AUEGT2SkyController;
class AUEGT2SurveyContract;
class SWidget;
class UUEGT2NPCDirector;
class UUEGT2SurveyContractSubsystem;

UCLASS()
class UEGT2_API UUEGT2ContractWalkSmokeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	/** Presence gates checkpoint IO even when the diagnostic arguments are invalid. */
	static bool IsRequested();
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bRequested && !bFinished; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

private:
	enum class EStage : uint8 { Startup, Settle, Focus, UsePending, Panel, Resume, Travel, Brake, ClaimFocus, Paid };
	void Start();
	bool ResolveCircuit();
	bool FindStanding(const FVector& Hint, FVector& Ground, FVector& Stand) const;
	void ObserveWorld(UWorld* World, ELevelTick TickType, float DeltaTime);
	bool CheckNormal(bool bWalking, bool bAllowPanel);
	bool CheckDiscoveries() const;
	bool CheckLedger();
	void BeginLeg();
	void ObserveTravel();
	void BeginInteraction();
	void ObserveInteraction();
	void ObservePanel();
	AUEGT2InteractableActor* InteractionTarget() const;
	void SetStage(EStage Next);
	void Key(FKey Which, EInputEvent Event);
	bool SlateKey(FKey Which);
	TSharedPtr<SWidget> FindButton(const FText& Caption) const;
	bool HasFocus(const FText& Caption) const;
	void LogState(const TCHAR* Reason) const;
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void Cleanup();

	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	TWeakObjectPtr<AUEGT2Character> Player;
	TWeakObjectPtr<AUEGT2SkyController> Sky;
	TWeakObjectPtr<UUEGT2NPCDirector> Director;
	TWeakObjectPtr<UUEGT2SurveyContractSubsystem> Contract;
	TWeakObjectPtr<AUEGT2SurveyContract> Board;
	TArray<TWeakObjectPtr<AUEGT2Landmark>> Markers;
	TWeakObjectPtr<AStaticMeshActor> Bridge;
	TWeakObjectPtr<UClass> LandscapeClass;
	TArray<FVector> Points;
	TArray<TArray<int32>> Legs;
	FVector LastPosition = FVector::ZeroVector;
	FKey ForwardKey, InteractKey;
	FString RunId;
	FDelegateHandle PostTickHandle;
	EStage Stage = EStage::Startup;
	int32 Leg = -1, Segment = 0, CompletedLegs = 0, Surveyed = 0;
	int32 Frames = 0, BridgeSamples[2][3] = {};
	uint8 Depleted = 0;
	double StartedAt = 0, StageStartedAt = 0, LegStartedAt = 0, LastLogAt = 0, LastProgressAt = 0;
	double WorldStartSeconds = 0, LastWorldSeconds = 0, WorldHours = 0, ObservedClockHours = 0;
	double LegWorldStart = 0, LegDeadline = 0, SegmentBase = 0, BestProgress = 0;
	double Distance2D = 0, Distance3D = 0, LegDistance2D = 0, LegDistance3D = 0, WorstCross = 0;
	double LastAlong = 0, LastCross = 0;
	float Rate = 0, PreviousExertion = 1, LastObservedExertion = 1, StartSkyHour = 0, LastSkyHour = 0, WorstFrame = 0;
	int32 StartDay = 0;
	FUEGT2NPCNeeds InitialNeeds, ClaimNeeds;
	FUEGT2Purse InitialPurse, ClaimPurse;
	EUEGT2NPCRole InitialTrade = EUEGT2NPCRole::Villager;
	int32 ClaimDay = 0;
	float ClaimHour = 0, ClaimSkyHour = 0;
	EUEGT2Weather ClaimWeather = EUEGT2Weather::Clear;
	bool bRequested = false, bFinished = false, bStarted = false;
	bool bForwardHeld = false, bInteractHeld = false, bInitialPanelDone = false;
};
