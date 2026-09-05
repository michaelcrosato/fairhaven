// Fairhaven - the durable part of one explorer's journey.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "NPC/UEGT2NPCTypes.h"
#include "UEGT2ProgressSave.generated.h"

/** No actor references: activities, followers and the simulated town are transient. */
UCLASS()
class UEGT2_API UUEGT2ProgressSave : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentSchemaVersion = 1;
	/** Bump when generated geography or persistent landmark identity changes incompatibly. */
	static constexpr int32 CurrentContentRevision = 1;
	static constexpr int32 MaxEncodedBytes = 256 * 1024;

	UPROPERTY() int32 SchemaVersion = CurrentSchemaVersion;
	UPROPERTY() int32 ContentRevision = CurrentContentRevision;
	UPROPERTY() int64 Sequence = 0;
	UPROPERTY() FString MapPackageName;
	/** Centre of the standing capsule, even when saved while crouched. */
	UPROPERTY() FVector PlayerLocation = FVector::ZeroVector;
	UPROPERTY() FRotator ViewRotation = FRotator::ZeroRotator;
	UPROPERTY() FUEGT2NPCNeeds Needs;
	UPROPERTY() FUEGT2Purse Purse;
	UPROPERTY() EUEGT2NPCRole Trade = EUEGT2NPCRole::Villager;
	UPROPERTY() int32 DayIndex = 0;
	UPROPERTY() float Hour = 10.5f;
	UPROPERTY() EUEGT2Weather Weather = EUEGT2Weather::Clear;
	UPROPERTY() TArray<FName> DiscoveredLandmarks;

	/** Validate the complete payload without touching the world. */
	bool Validate(const FString& ExpectedMap, const TSet<FName>& KnownLandmarks, FText& OutReason) const;
	/** Integrity is checked before Unreal is allowed to read any variable-length field. */
	bool Encode(TArray<uint8>& OutBytes);
	static UUEGT2ProgressSave* Decode(const TArray<uint8>& Bytes, FText& OutReason);
};
