#pragma once

#include "CoreMinimal.h"

/** Byte transport only. Async callbacks run on the game thread. */
class IUEGT2CheckpointStorage
{
public:
	enum class EPresence { Present, Missing, Unreadable };
	using FExistsComplete = TFunction<void(EPresence)>;
	using FReadComplete = TFunction<void(bool, const TArray<uint8>&)>;
	using FWriteComplete = TFunction<void(bool)>;
	virtual ~IUEGT2CheckpointStorage() = default;
	virtual bool Exists(const FString& Slot) = 0;
	virtual bool Read(const FString& Slot, TArray<uint8>& Bytes) = 0;
	virtual bool Write(const FString& Slot, const TArray<uint8>& Bytes) = 0;
	virtual void ExistsAsync(const FString& Slot, FExistsComplete Complete) = 0;
	virtual void ReadAsync(const FString& Slot, FReadComplete Complete) = 0;
	virtual void WriteAsync(const FString& Slot, const TArray<uint8>& Bytes, FWriteComplete Complete) = 0;
};

TSharedRef<IUEGT2CheckpointStorage> UEGT2CreateCheckpointStorage();
