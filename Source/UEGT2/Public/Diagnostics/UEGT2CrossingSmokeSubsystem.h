// Explicit packaged test of the normal player capsule on the lower river bridge.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2CrossingSmokeSubsystem.generated.h"

class AStaticMeshActor;
class AUEGT2Character;
class AUEGT2PlayerController;
class AUEGT2SkyController;

UCLASS()
class UEGT2_API UUEGT2CrossingSmokeSubsystem : public UTickableWorldSubsystem
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
	enum class EStage : uint8 { Startup, Settle, Outbound, Turnaround, Inbound, Brake };
	void Start();
	void ObserveWorld(UWorld* World, ELevelTick TickType, float DeltaTime);
	bool ResolveBridge();
	bool FindDryEndpoint(int32 SocketIndex, const FVector& Outward, FVector& Ground, FVector& Stand);
	bool CheckNormal(bool bRequireWalking);
	bool IsTerrain(const AActor* Actor) const;
	void BeginLeg(bool bReturn);
	void ObserveLeg();
	void Key(EInputEvent Event);
	void LogState(const TCHAR* Reason) const;
	bool Check(bool bCondition, const TCHAR* Reason);
	void Finish(bool bSuccess, const TCHAR* Reason);
	void Cleanup();

	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	TWeakObjectPtr<AUEGT2Character> Player;
	TWeakObjectPtr<AUEGT2SkyController> Sky;
	TWeakObjectPtr<AStaticMeshActor> Bridge;
	TWeakObjectPtr<UClass> LandscapeClass;
	// Dry A, ApproachA, DeckA, DeckB, ApproachB, dry B: ground surface positions.
	TArray<FVector> Points;
	FVector StartStand = FVector::ZeroVector;
	FVector LastPosition = FVector::ZeroVector;
	FKey ForwardKey;
	FString RunId;
	FDelegateHandle PostTickHandle;
	EStage Stage = EStage::Startup;
	int32 Segment = 0;
	int32 Frames = 0;
	int32 SupportFrames[3] = { 0, 0, 0 };
	int32 CompletedLegs = 0;
	double StartedAt = 0.0;
	double StageStartedAt = 0.0;
	double LegStartedAt = 0.0;
	double LastProgressAt = 0.0;
	double LastLogAt = 0.0;
	double SegmentBase = 0.0;
	double BestProgress = 0.0;
	double Distance = 0.0;
	double WorstCross = 0.0;
	double LastAlong = 0.0;
	double LastCross = 0.0;
	bool bRequested = false;
	bool bFinished = false;
	bool bClockChanged = false;
	bool bOriginalClock = false;
	bool bKeyHeld = false;
};
